#include "media/AudioSpectrum.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "util/AppLog.h"

namespace media {

namespace {

constexpr int kFftSize = 1024;
constexpr float kSampleRateFallback = 48000.0f;
constexpr float kPi = 3.14159265358979323846f;

const GUID kClsidMmDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidMmDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidAudioClient = {
    0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const GUID kIidAudioCaptureClient = {
    0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};

std::atomic<bool> gRunning{false};
std::thread gWorker;
std::mutex gBandMutex;
std::array<float, AudioSpectrum::kBands> gBands{};

void publishBands(const std::array<float, AudioSpectrum::kBands>& bands) {
    std::lock_guard<std::mutex> lock(gBandMutex);
    gBands = bands;
}

// 当前默认输出设备 id（用于检测「用户切换了播放设备」，比如插拔耳机）
std::wstring queryDefaultDeviceId(IMMDeviceEnumerator* enumerator) {
    std::wstring result;
    if (enumerator == nullptr) return result;
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
        device != nullptr) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id)) && id != nullptr) {
            result.assign(id);
            CoTaskMemFree(id);
        }
        device->Release();
    }
    return result;
}

// 原地基 2 FFT
void fft(std::vector<float>& real, std::vector<float>& imag) {
    const int n = (int)real.size();
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        while ((j & bit) != 0) {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }
    for (int length = 2; length <= n; length <<= 1) {
        const float angle = -2.0f * kPi / (float)length;
        const float wRealStep = std::cos(angle);
        const float wImagStep = std::sin(angle);
        for (int i = 0; i < n; i += length) {
            float wReal = 1.0f;
            float wImag = 0.0f;
            for (int k = 0; k < length / 2; ++k) {
                const int u = i + k;
                const int v = i + k + length / 2;
                const float tReal = real[v] * wReal - imag[v] * wImag;
                const float tImag = real[v] * wImag + imag[v] * wReal;
                real[v] = real[u] - tReal;
                imag[v] = imag[u] - tImag;
                real[u] += tReal;
                imag[u] += tImag;
                const float nextWReal = wReal * wRealStep - wImag * wImagStep;
                wImag = wReal * wImagStep + wImag * wRealStep;
                wReal = nextWReal;
            }
        }
    }
}

// 把 FFT 结果压成 32 个对数频段（25Hz..16kHz），带时间平滑（对齐 Java 版）
void computeBands(const std::vector<float>& real, const std::vector<float>& imag, float sampleRate,
                  std::array<float, AudioSpectrum::kBands>& smooth) {
    const int bands = AudioSpectrum::kBands;
    const int bins = kFftSize / 2;
    const float nyquist = sampleRate / 2.0f;
    const float minF = 25.0f;
    const float maxF = std::min(16000.0f, nyquist * 0.95f);
    const float logMin = std::log(minF);
    const float logMax = std::log(maxF);
    float peak = 0.0f;
    std::array<float, AudioSpectrum::kBands> raw{};
    for (int b = 0; b < bands; ++b) {
        const float f0 = std::exp(logMin + (logMax - logMin) * b / bands);
        const float f1 = std::exp(logMin + (logMax - logMin) * (b + 1) / bands);
        const int i0 = std::max(1, (int)(f0 / (sampleRate / kFftSize)));
        const int i1 = std::min(bins - 1, std::max(i0, (int)(f1 / (sampleRate / kFftSize))));
        float sum = 0.0f;
        for (int i = i0; i <= i1; ++i) {
            sum += std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
        }
        const int count = std::max(1, i1 - i0 + 1);
        const float magnitude = sum / (float)count;
        raw[b] = magnitude;
        if (magnitude > peak) peak = magnitude;
    }
    if (peak <= 1e-6f) {
        for (int b = 0; b < bands; ++b) smooth[b] *= 0.85f;
        return;
    }
    for (int b = 0; b < bands; ++b) {
        const float normalized = std::clamp(raw[b] / peak, 0.0f, 1.0f);
        const float boosted = std::sqrt(normalized);
        smooth[b] = smooth[b] * 0.45f + boosted * 0.55f;
    }
}

// 一次采集会话：绑定「当前默认输出设备」做 loopback，直到采集失败 / 设备切换 / 被要求停止。
// 返回 true 表示正常跑过采集（可能是设备切换导致结束），false 表示初始化失败。
bool runCaptureSession() {
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    WAVEFORMATEX* mixFormat = nullptr;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    bool comInitialized = false;

    auto cleanup = [&]() {
        if (captureClient != nullptr) {
            captureClient->Release();
            captureClient = nullptr;
        }
        if (audioClient != nullptr) {
            audioClient->Stop();
            audioClient->Release();
            audioClient = nullptr;
        }
        if (mixFormat != nullptr) {
            CoTaskMemFree(mixFormat);
            mixFormat = nullptr;
        }
        if (device != nullptr) {
            device->Release();
            device = nullptr;
        }
        if (enumerator != nullptr) {
            enumerator->Release();
            enumerator = nullptr;
        }
        if (comInitialized) CoUninitialize();
    };

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        cleanup();
        return false;
    }
    comInitialized = true;

    if (FAILED(CoCreateInstance(kClsidMmDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIidMmDeviceEnumerator, (void**)&enumerator)) ||
        enumerator == nullptr) {
        AppLog::warn("AudioSpectrum", "无法创建 MMDeviceEnumerator，频谱不可用");
        cleanup();
        return false;
    }
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
        device == nullptr) {
        cleanup();
        return false;
    }
    if (FAILED(device->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, (void**)&audioClient)) ||
        audioClient == nullptr) {
        cleanup();
        return false;
    }
    if (FAILED(audioClient->GetMixFormat(&mixFormat)) || mixFormat == nullptr) {
        cleanup();
        return false;
    }
    const int channels = (int)mixFormat->nChannels;
    const int sampleRate = (int)mixFormat->nSamplesPerSec;
    const int bits = (int)mixFormat->wBitsPerSample;
    if (channels <= 0 || sampleRate <= 0) {
        cleanup();
        return false;
    }
    const bool isFloat = bits == 32;
    const bool isInt16 = bits == 16;
    if (!isFloat && !isInt16) {
        AppLog::info("AudioSpectrum", "不支持的采样格式 bits=" + std::to_string(bits));
        cleanup();
        return false;
    }
    const int bytesPerSample = bits / 8;
    const int frameBytes = bytesPerSample * channels;

    if (FAILED(audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                       10000000LL, 0, mixFormat, nullptr))) {
        AppLog::warn("AudioSpectrum", "loopback 初始化失败");
        cleanup();
        return false;
    }
    if (FAILED(audioClient->GetService(kIidAudioCaptureClient, (void**)&captureClient)) ||
        captureClient == nullptr) {
        cleanup();
        return false;
    }
    if (FAILED(audioClient->Start())) {
        cleanup();
        return false;
    }
    // 记住当前绑定的默认设备，用于检测切换
    const std::wstring currentDeviceId = queryDefaultDeviceId(enumerator);
    AppLog::info("AudioSpectrum", "频谱采集启动 " + std::to_string(channels) + "ch " +
                                      std::to_string(sampleRate) + "Hz " + std::to_string(bits) +
                                      "bit");

    const int ringSize = kFftSize * 8;
    std::vector<float> ring((size_t)ringSize, 0.0f);
    int ringPos = 0;
    int filled = 0;
    std::vector<float> fftReal((size_t)kFftSize);
    std::vector<float> fftImag((size_t)kFftSize);
    std::vector<float> window((size_t)kFftSize);
    for (int i = 0; i < kFftSize; ++i) {
        window[(size_t)i] = 0.5f - 0.5f * std::cos(2.0f * kPi * i / (float)(kFftSize - 1));
    }
    std::array<float, AudioSpectrum::kBands> smooth{};
    std::vector<unsigned char> packetBytes;
    uint64_t lastAnalyze = 0;
    uint64_t lastDeviceCheck = GetTickCount64();

    while (gRunning.load()) {
        UINT32 packetFrames = 0;
        if (FAILED(captureClient->GetNextPacketSize(&packetFrames))) break;
        while (packetFrames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                packetFrames = 0;
                break;
            }
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            if (!silent && data != nullptr && frames > 0) {
                const size_t bytes = (size_t)frames * (size_t)frameBytes;
                packetBytes.resize(bytes);
                memcpy(packetBytes.data(), data, bytes);
                size_t offset = 0;
                for (UINT32 f = 0; f < frames; ++f) {
                    float sum = 0.0f;
                    for (int c = 0; c < channels; ++c) {
                        if (isFloat) {
                            float value = 0.0f;
                            memcpy(&value, packetBytes.data() + offset, 4);
                            sum += value;
                            offset += 4;
                        } else {
                            int16_t value = 0;
                            memcpy(&value, packetBytes.data() + offset, 2);
                            sum += (float)value / 32768.0f;
                            offset += 2;
                        }
                    }
                    ring[(size_t)ringPos] = sum / (float)channels;
                    ringPos = (ringPos + 1) % ringSize;
                    if (filled < ringSize) ++filled;
                }
            }
            captureClient->ReleaseBuffer(frames);
            if (FAILED(captureClient->GetNextPacketSize(&packetFrames))) {
                packetFrames = 0;
            }
        }

        const uint64_t now = GetTickCount64();
        // 用户切换输出设备（插拔耳机 / 系统里改默认设备）后，旧 loopback 抓的是已静音的设备，
        // 音律条会「没反应」。这里每秒比对一次默认设备，变了就重建采集会话。
        if (now - lastDeviceCheck >= 1000) {
            lastDeviceCheck = now;
            const std::wstring latestDeviceId = queryDefaultDeviceId(enumerator);
            if (!latestDeviceId.empty() && latestDeviceId != currentDeviceId) {
                AppLog::info("AudioSpectrum", "默认输出设备已切换，重建频谱采集");
                break;
            }
        }
        if (filled >= kFftSize && now - lastAnalyze >= 33) {
            lastAnalyze = now;
            const int start = (ringPos - kFftSize + ringSize) % ringSize;
            for (int i = 0; i < kFftSize; ++i) {
                fftReal[(size_t)i] = ring[(size_t)((start + i) % ringSize)] * window[(size_t)i];
                fftImag[(size_t)i] = 0.0f;
            }
            fft(fftReal, fftImag);
            computeBands(fftReal, fftImag, sampleRate > 0 ? (float)sampleRate
                                                          : kSampleRateFallback,
                         smooth);
            publishBands(smooth);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
    }

    cleanup();
    return true;
}

void runCapture() {
    while (gRunning.load()) {
        if (!runCaptureSession()) {
            // 初始化失败（没有输出设备 / 独占模式等）：等一会儿再试，避免空转刷日志
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        if (!gRunning.load()) break;
    }
    gRunning = false;
}

}  // namespace

void AudioSpectrum::start() {
    if (gRunning.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lock(gBandMutex);
        gBands.fill(0.0f);
    }
    if (gWorker.joinable()) gWorker.join();
    gWorker = std::thread(runCapture);
    gWorker.detach();
}

void AudioSpectrum::stop() { gRunning = false; }

std::array<float, AudioSpectrum::kBands> AudioSpectrum::bands() {
    std::lock_guard<std::mutex> lock(gBandMutex);
    return gBands;
}

}  // namespace media
