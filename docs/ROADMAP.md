# 路线图（C++ 桌面版）

目标：在保留 Kotlin/Java 版的同时，用 C++ 做一版原生桌面端；功能对齐后可以作为轻量、免 JRE 的分发版本。

## M1 · 工程骨架 + 播放内核（已完成）

- [x] CMake + MinGW-w64（w64devkit）构建，不依赖 Qt / vcpkg / Visual Studio
- [x] 依赖源码内嵌：Dear ImGui 1.93.0、GLFW 3.5.1、nlohmann/json 3.12.0
- [x] 深色 UI 骨架：左侧歌单栏、右侧曲目表、底部播放条（暂停/停止/进度/音量）
- [x] 读取 Java 版数据文件 `%USERPROFILE%\.bilimusic\playlists.json`（字段完全兼容）
- [x] libVLC 运行时加载：优先 `exe 所在目录\vlc`，其次 `BILIMUSIC_VLC_DIR`、系统安装的 VLC
- [x] 本地文件播放 / 进度显示 / seek / 音量（已实测：本地音乐 38 首，双击播放进度正常推进）
- [x] 静态链接 MinGW 运行时（免 libstdc++-6.dll 等依赖，单 exe 可直接启动）

## M1.5 · UI 与模块化组件（已完成）

- [x] 无边框自绘标题栏（拖动 / 双击最大化 / 最小化 / 最大化 / 还原 / 关闭）
- [x] 侧栏 / 歌单页 / 播放条三个页面模块，布局尺寸与 Java 版 BmDimens 对齐
- [x] 可复用控件库：IconButton、PrimaryButton、GhostButton、SidebarAction、PlayCircleButton、ThinSlider、CoverPlaceholder、TagChip
- [x] Material Icons 字体合并进正文字体，按钮图标与 Java 版对应
- [x] 播放模式按钮、上一首/下一首、进度拖动、音量拖动已接到 libVLC
- [x] emoji 标题显示降级（替换成音符图标，数据不动）

## M1.6 · 交互与渲染修复（已完成）

- [x] 应用级音量：WASAPI ISimpleAudioVolume，音量绕开 VLC（即时生效），不可用时回退 libvlc
- [x] 原生无边框窗口：WM_NCHITTEST → HTCAPTION 系统拖动、HT* 四边缩放、WM_NCCALCSIZE 去边框、WM_GETMINMAXINFO 工作区最大化
- [x] 封面圆角/头像抗锯齿：渐变条带 + 四角扇形（AA），不再有逐行台阶
- [x] 修复音量条每帧被重置回 80% 的问题

## M2 · B 站接口与在线播放（已完成）

- [x] WBI 签名（nav → mixin_key 重排、wts、md5 w_rid，24h 缓存）
- [x] Cookie 与 Java 版共用 settings.json（cookie_blob），启动校验 nav、显示昵称/UID
- [x] 登录弹窗（粘贴 Cookie）/ 退出登录
- [x] view 接口取 cid/标题/UP主；wbi/playurl 取流 + 强刷 key 重试 + 备选接口
- [x] 音质降级链（指定档位 → 192K → 132K → 64K → 最高带宽）与 durl 兜底
- [x] 在线播放（libVLC + Referer/UA）、上一首/下一首自动取流、粘贴 BV 弹窗
- [x] 音质档位 UI（自动/192K/132K/64K/Hi-Res/杜比，写入 audio_quality 与 Java 共用）
- [x] 备用 URL 自动切换 + 起播无进度时强制重新取流（URL 过期兜底）
- [x] 网络缓存 20s
- [x] 扫码登录（qrcodegen 编码 + poll 轮询 + Set-Cookie 导入）

## M2 之后

从 Kotlin 版 `shared/` 1:1 移植：

- [ ] WBI 签名（`WbiSigner.kt`，MD5 + 混淆表）
- [ ] Cookie 登录（扫码登录 + 手动粘贴 Cookie）、Cookie 持久化
- [ ] 取流接口（`/x/player/wbi/playurl`）+ 音质降级链（192K → 132K → 64K → Hi-Res/杜比）
- [ ] 视频详情（拿 cid）、URL 过期重取（5 分钟安全余量）
- [ ] 在线播放：libvlc 需要带 `:http-referrer=https://www.bilibili.com/` 和 UA

## M3 · 歌单导入与管理

- [ ] 收藏夹列表 / 稍后再看 / UP主投稿 / 合集（season）导入，实时进度条
- [ ] 歌单新建 / 重命名 / 删除 / 排序（重名规则对齐 Java 版：覆盖 / 自动重命名 (n) / 取消）
- [ ] 曲目多选：复制到歌单 / 移动到歌单 / 删除
- [ ] 离线缓存：下载当前曲目到 ~/.bilimusic/downloads/，离线优先播放

## M4 · 体验对齐

- [x] 封面图（网络图异步缓存 + 自定义本地图，ContentScale.Crop 裁切）
- [x] 沉浸模式（全屏 / 封面背景 / 大封面 / 进度 + 控制 / F11·Esc）
- [x] 播放模式（顺序 / 列表循环 / 单曲循环 / 随机）
- [x] 系统托盘（图标 + 菜单：显示窗口 / 播放暂停 / 上一首 / 下一首 / 退出；关闭收进托盘）
- [x] 全局媒体快捷键（媒体键 播放暂停 / 上一首 / 下一首）
- [x] 定时关闭、防熄屏
- [x] 输出设备选择（WASAPI/mmdevice 枚举 + 切换 + 持久化）
- [x] 播放队列面板（高亮当前、点击切歌）
- [x] 本地音乐扫描（扫描目录）
- [x] 下载当前曲目到 ~/.bilimusic/downloads

## M5 · 打包分发

- [ ] 内置 VLC（复用 Java 版打包脚本的 vlc/ 目录，369 文件 / 135MB）
- [ ] 免安装 zip + 自解压安装器（7z SFX，思路同 Java 版 desktop/tools/build-exe.ps1）
- [ ] 体积/内存/启动时间对比 Java 版，记录在 README

## 功能对等清单（用来判断什么时候能替换 Java 版）

| 能力 | Java 版 | C++ M1 | 目标里程碑 |
| --- | --- | --- | --- |
| 本地音乐播放 | 支持 | 支持 | M1 |
| 在线播放（WBI 取流） | 支持 | 待做 | M2 |
| 扫码/Cookie 登录 | 支持 | 待做 | M2 |
| 四种来源导入歌单 | 支持 | 待做 | M3 |
| 歌单重名处理 | 支持 | 待做 | M3 |
| 离线缓存 | 支持 | 待做 | M3 |
| 沉浸模式 / 封面 / 歌词 | 支持 | 待做 | M4 |
| 托盘 / 全局快捷键 / 防熄屏 | 支持 | 待做 | M4 |
| 打包体积/内存 | Working Set 约 430MB | 待测 | M5 |

## 技术决策记录

- **UI 用 Dear ImGui 而不是 Qt**：M1 阶段优先「零第三方 SDK、构建简单、立刻能迭代」。Qt 需要安装 Qt SDK（几 GB）或 vcpkg 编译数小时，对当前目标不划算。ImGui 外观可定制到接近深色音乐播放器，但复杂动画/富文本能力有限；如果后续需要在 UI 上投入更多，可以评估迁移到 Qt6（业务层与 UI 解耦）。
- **libVLC 动态加载（LoadLibrary + GetProcAddress）**：不依赖 VLC SDK 头文件/import lib，打包时只要带 libvlc.dll + libvlccore.dll + plugins 即可；后续要完整 API（事件、输出设备）时可换成官方 vlc/*.h。
- **数据格式与 Java 版共用**：playlists.json 直接读，两个版本可以来回切换、不打架。
- **MinGW 静态链接**：避免安装版还要附带 libstdc++/libgcc/winpthread 三个 DLL。
