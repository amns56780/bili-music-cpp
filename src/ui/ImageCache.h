#pragma once

#include <imgui.h>

#include <string>

// 封面图纹理缓存：
// - 本地文件（coverPath）同步加载
// - 网络图片（coverUrl）后台线程下载，下一帧在主线程上传成 GL 纹理
// 没准备好时返回 0，调用方退化成渐变占位。
namespace Ui {

ImTextureID CoverTextureFor(const std::string& coverUrl, const std::string& coverPath);

// 查询纹理原始像素尺寸（用于按 Java 版 ContentScale.Crop 居中裁切）
bool CoverTextureSize(ImTextureID texture, int* width, int* height);

// BM 应用图标纹理（底部播放条空闲时显示）
ImTextureID AppIconTexture();

// 沉浸模式背景：桌面壁纸 / 指定图片，做高斯近似模糊后返回纹理（失败返回 0）
ImTextureID BlurredWallpaperTexture();
ImTextureID BlurredImageTexture(const std::string& path);

// 主线程每帧调用：把后台下载完成的字节上传成 OpenGL 纹理
void PumpImageCache();

void ClearImageCache();

}  // namespace Ui
