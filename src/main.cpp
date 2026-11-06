#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <shobjidl.h>  // SetCurrentProcessExplicitAppUserModelID（任务栏分组/图标）

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "app/App.h"
#include "bili/BiliService.h"
#include "media/AppVolume.h"
#include "media/VlcEngine.h"
#include "platform/SystemTray.h"
#include "ui/AppIcon.h"
#include "ui/ImageCache.h"
#include "ui/WindowFrame.h"

namespace {

void glfwErrorCallback(int code, const char* description) {
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "GLFW error %d: %s", code,
                  description ? description : "(no description)");
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
    std::fputs(buffer, stderr);
    std::fputc('\n', stderr);
}


std::wstring utf8ToWidePath(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size);
    return out;
}

// 暂停/恢复的客观自检：用 WASAPI 峰值表采样，统计「静音采样点」比例。
// 用法：bilimusic.exe --test-pause <本地音频路径>
//       bilimusic.exe --test-pause-online BV1xx411c7mD
int runPauseTest(const std::string& mode, const std::string& target) {
    const bool online = mode == "--test-pause-online";
    const bool usePlayResume = std::getenv("BILIMUSIC_RESUME_PLAY") != nullptr;
    const bool skipVolume = std::getenv("BILIMUSIC_TEST_NO_VOLUME") != nullptr;
    const char* referrer = "https://www.bilibili.com/";
    const char* userAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/126.0.0.0 Safari/537.36";

    VlcEngine engine;
    std::string error;
    if (!engine.init(&error)) {
        std::printf("engine init failed: %s\n", error.c_str());
        return 1;
    }
    media::AppVolume volume;
    volume.init();

    bool played = false;
    std::string title;
    if (online) {
        bili::BiliService service;
        service.init();
        const std::string bvid = bili::BiliService::parseBvid(target);
        const bili::VideoInfo video = service.fetchVideo(bvid);
        if (!video.ok) {
            std::printf("view failed: %s\n", video.message.c_str());
            return 2;
        }
        const bili::AudioStreamInfo stream = service.resolveAudio(bvid, video.cid, 0);
        if (!stream.ok) {
            std::printf("resolve failed: %s\n", stream.message.c_str());
            return 3;
        }
        title = video.title;
        played = engine.playUrl(stream.url, referrer, userAgent, &error);
    } else {
        title = target;
        played = engine.playPath(utf8ToWidePath(target), &error);
    }
    std::printf("play ok=%d err=%s title=%s\n", played ? 1 : 0, error.c_str(), title.c_str());
    if (!played) return 4;

    std::this_thread::sleep_for(std::chrono::seconds(3));
    // 先调用一次拿到音频会话（峰值表挂在会话上）；后面是否持续写音量由 skipVolume 控制
    volume.setVolume(80, true);

    auto sample = [&](const char* tag, int seconds) {
        int samples = 0;
        int zeros = 0;
        float maxPeak = 0.0f;
        for (int i = 0; i < seconds * 20; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (!skipVolume) volume.setVolume(80, true);
            const float peak = volume.peak();
            if (peak < 0.0f) continue;
            ++samples;
            if (peak < 0.0005f) ++zeros;
            if (peak > maxPeak) maxPeak = peak;
        }
        std::printf("%s: samples=%d silent=%d (%.1f%%) maxPeak=%.4f\n", tag, samples, zeros,
                    samples > 0 ? 100.0 * zeros / samples : 0.0, maxPeak);
    };

    sample("baseline", 3);
    engine.togglePause();
    // 模拟用户暂停一会儿（超过 1.5s 的恢复 seek 阈值）
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    if (usePlayResume) {
        engine.resumeByPlay();
    } else {
        engine.togglePause();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    sample("after-resume", 8);
    std::printf("resume-mode=%s skipVolume=%d\n", usePlayResume ? "play" : "set_pause",
                skipVolume ? 1 : 0);
    engine.shutdown();
    volume.shutdown();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // 让任务栏把窗口归到 bilimusic 而不是 python/exe 的默认组，图标才会用我们的 exe 图标
    SetCurrentProcessExplicitAppUserModelID(L"amns56780.bilimusic");
    if (argc > 2 && (std::string(argv[1]) == "--test-pause" ||
                     std::string(argv[1]) == "--test-pause-online")) {
        return runPauseTest(argv[1], argv[2]);
    }

    // 控制台自检：.ilimusic.exe --check-bili BV1xx411c7mD
    // 用来在不启动 UI 的情况下验证 Cookie / WBI 签名 / 取流链路。
    // 扫码登录自检：.\bilimusic.exe --qr-test
    if (argc > 1 && std::string(argv[1]) == "--qr-test") {
        bili::BiliService service;
        service.init();
        const bili::QrCodeInfo qr = service.qrGenerate();
        std::printf("qr generate ok=%d url=%s key=%s msg=%s\n", qr.ok ? 1 : 0, qr.url.c_str(),
                    qr.key.c_str(), qr.message.c_str());
        if (!qr.ok) return 1;
        const bili::QrPollInfo poll = service.qrPoll(qr.key);
        std::printf("qr poll ok=%d success=%d scanned=%d expired=%d cookies=%zu msg=%s\n",
                    poll.ok ? 1 : 0, poll.success ? 1 : 0, poll.scanned ? 1 : 0,
                    poll.expired ? 1 : 0, poll.cookies.size(), poll.message.c_str());
        return 0;
    }

    if (argc > 2 && std::string(argv[1]) == "--check-bili") {
        bili::BiliService service;
        service.init();
        const bili::NavInfo nav = service.fetchNav();
        std::printf("nav ok=%d loggedIn=%d uname=%s mid=%lld msg=%s\n", nav.ok ? 1 : 0,
                    nav.loggedIn ? 1 : 0, nav.userName.c_str(), nav.userId, nav.message.c_str());

        const std::string bvid = bili::BiliService::parseBvid(argv[2]);
        std::printf("bvid=%s\n", bvid.c_str());
        if (bvid.empty()) return 2;

        const bili::VideoInfo video = service.fetchVideo(bvid);
        std::printf("video ok=%d bvid=%s cid=%lld title=%s artist=%s msg=%s\n", video.ok ? 1 : 0,
                    video.bvid.c_str(), video.cid, video.title.c_str(), video.artist.c_str(),
                    video.message.c_str());
        if (!video.ok) return 3;

        const bili::AudioStreamInfo stream = service.resolveAudio(bvid, video.cid, 0);
        std::printf("stream ok=%d quality=%s downgraded=%d url=%.120s msg=%s\n", stream.ok ? 1 : 0,
                    stream.qualityLabel().c_str(), stream.downgraded ? 1 : 0, stream.url.c_str(),
                    stream.message.c_str());
        return stream.ok ? 0 : 4;
    }

    // ---------------- 单实例 ----------------
    // 已有实例在跑：把它的窗口唤到前台（即使收进了托盘），本进程直接退出，
    // 避免两个窗口共用同一份数据互相覆盖设置。
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\bilimusic-single-instance");
    if (instanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        bool signaled = false;
        for (int attempt = 0; attempt < 20 && !signaled; ++attempt) {
            HANDLE showEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\bilimusic-show-window");
            if (showEvent != nullptr) {
                SetEvent(showEvent);
                CloseHandle(showEvent);
                signaled = true;
                break;
            }
            Sleep(100);
        }
        if (!signaled) {
            if (HWND existing = FindWindowW(nullptr, L"bilimusic")) {
                ShowWindow(existing, SW_SHOW);
                ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
            }
        }
        CloseHandle(instanceMutex);
        return 0;
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // 无边框窗口：标题栏由 Ui::DrawTitleBar 自己画（和 Java 版一致）
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    // 窗口标题和 Java 版保持一致（"bilimusic"），方便沿用旧的截图/自动化脚本
    GLFWwindow* window = glfwCreateWindow(1240, 860, "bilimusic", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    // 和 Java 版同款应用图标（任务栏 / Alt+Tab / 窗口图标）
    {
        GLFWimage images[Ui::AppIcon::kImageCount];
        for (int i = 0; i < Ui::AppIcon::kImageCount; ++i) {
            images[i].width = Ui::AppIcon::kImages[i].width;
            images[i].height = Ui::AppIcon::kImages[i].height;
            images[i].pixels = const_cast<unsigned char*>(Ui::AppIcon::kImages[i].pixels);
        }
        glfwSetWindowIcon(window, Ui::AppIcon::kImageCount, images);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetWindowSizeLimits(window, 1000, 640, GLFW_DONT_CARE, GLFW_DONT_CARE);
    // 无边框窗口交给原生窗口过程：标题栏原生拖动 + 四边原生缩放（Java 版被 AWT 挡住做不到）
    Ui::InstallWindowFrame(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // 不写 imgui.ini，窗口布局由代码决定
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    App app;
    app.init(window);

    HWND nativeWindow = glfwGetWin32Window(window);
    bool exitRequested = false;
    // 第二个实例用这个事件唤醒本进程的窗口（收进托盘时也能拉回来）
    HANDLE showWindowEvent = CreateEventW(nullptr, FALSE, FALSE, L"Local\\bilimusic-show-window");
    platform::SystemTray tray;
    platform::TrayCallbacks trayCallbacks;
    trayCallbacks.onShowWindow = [&]() {
        ShowWindow(nativeWindow, SW_SHOW);
        ShowWindow(nativeWindow, SW_RESTORE);
        SetForegroundWindow(nativeWindow);
        glfwFocusWindow(window);
    };
    trayCallbacks.onTogglePlay = [&]() { app.togglePlayback(); };
    trayCallbacks.onNext = [&]() { app.playNext(); };
    trayCallbacks.onPrevious = [&]() { app.playPrevious(); };
    trayCallbacks.onExit = [&]() {
        exitRequested = true;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    };
    tray.init("bilimusic", trayCallbacks);

    bool preventSleep = false;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        // 后台（收进托盘）也要跑播放状态/自动切歌/异步事件，不能只在 draw() 里做
        app.tick();
        // 又启动了一次 → 把已有窗口拉到前台
        if (showWindowEvent != nullptr &&
            WaitForSingleObject(showWindowEvent, 0) == WAIT_OBJECT_0) {
            trayCallbacks.onShowWindow();
        }
        // 应用内点了「卸载」→ 真正退出（否则会被当成收进托盘）
        if (app.quitRequested()) {
            exitRequested = true;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        // 播放时阻止熄屏/休眠（M4）
        const bool wantPreventSleep = app.playbackActive();
        if (wantPreventSleep != preventSleep) {
            SetThreadExecutionState(wantPreventSleep
                                        ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
                                        : ES_CONTINUOUS);
            preventSleep = wantPreventSleep;
        }
        if (glfwWindowShouldClose(window)) {
            // 关闭按钮 = 收进托盘（托盘退出才真正退出）
            if (!exitRequested && tray.active()) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                ShowWindow(nativeWindow, SW_HIDE);
                app.saveWindowState();
                continue;
            }
            break;
        }
        if (IsWindowVisible(nativeWindow) == FALSE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        Ui::PumpImageCache();
        app.draw();

        ImGui::Render();
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.039f, 0.039f, 0.047f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    SetThreadExecutionState(ES_CONTINUOUS);
    tray.shutdown();
    app.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
