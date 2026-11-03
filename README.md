# bilimusic · C++ 桌面版（重构中）

> 这是 [bilimusic](https://github.com/amns56780/bili-music) 桌面端的 **C++ 重写版**。
> 原来的 Kotlin / Compose 版本继续维护、不受影响；这里是并行的新实现。

## 为什么另起一版

- 去掉对 **JRE/JDK 17+** 的依赖：Java 版安装包必须要求机器上装了 Java，C++ 版是原生 exe
- 更小的体积和内存占用、更快的启动（Java 版实测启动约 3.2s、Working Set 约 430MB）
- 顺带把播放内核（libVLC）的封装换成直接 C API，不再经过 VLCJ/JNA

## 当前进度（M1）

- [x] CMake + MinGW-w64 构建，依赖全部内嵌在 `third_party/`（ImGui / GLFW / nlohmann-json），不需要 Qt、vcpkg、Visual Studio
- [x] 深色界面骨架：左侧歌单栏 + 右侧曲目表 + 底部播放条
- [x] 读取 Java 版同一份数据文件 `%USERPROFILE%\\.bilimusic\\playlists.json`
- [x] libVLC 动态加载（优先 `程序目录\\vlc`，其次系统安装的 VLC），支持本地文件播放 / 暂停 / 停止 / 进度拖动 / 音量
- [ ] M2：B 站接口（WBI 签名 / Cookie 登录 / 取流）+ 在线播放
- [ ] M3：歌单导入与管理（收藏夹 / 稍后再看 / UP主投稿 / 合集）
- [ ] M4：沉浸模式 / 封面 / 歌词 / 托盘 / 快捷键
- [ ] M5：打包（内置 VLC + 免安装 zip + 安装器）

## 构建

要求：CMake 3.20+、MinGW-w64（g++ 13+）。**不需要 Qt / vcpkg / Visual Studio。**

```powershell
# 用仓库自带的脚本（自动找 ../tools 下便携版 MinGW + CMake）
.\\scripts\\build.ps1

# 或者手动
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

产物：`build\\bilimusic.exe`（运行时需要能加载 libVLC：优先找 exe 旁边的 `vlc\\libvlc.dll`，
其次系统安装的 VLC；也可以设置环境变量 `BILIMUSIC_VLC_DIR` 指向 VLC 目录）。

## 目录结构

```
src/
  main.cpp              GLFW + ImGui 生命周期
  app/App.{h,cpp}       UI 与应用状态
  media/VlcEngine.{h,cpp}  libVLC 动态加载封装（播放/暂停/进度/音量）
  data/LibraryStore.{h,cpp} 读取 playlists.json（与 Java 版共用格式）
third_party/            imgui / glfw / nlohmann-json（源码内嵌）
scripts/build.ps1       一键构建
```

## 和 Java 版的关系

- 数据目录、`playlists.json` 格式完全兼容，两个版本可以共用一个数据目录
- 接口相关逻辑（WBI 签名、Cookie、取流）会在 M2 从 Kotlin 版 1:1 移植到 C++
- Java 版继续作为稳定版；C++ 版达到功能对等后再决定分发策略

## 依赖与许可

| 依赖 | 版本 | 许可 |
| --- | --- | --- |
| Dear ImGui | 1.93.0 | MIT |
| GLFW | 3.5.1 | zlib/libpng |
| nlohmann/json | 3.12.0 | MIT |
| libVLC | 3.x（运行时） | LGPL/GPL |

本项目代码：MIT（见 `LICENSE`）。
