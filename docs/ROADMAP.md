# 路线图（C++ 桌面版）

目标：在保留 Kotlin/Java 版的同时，用 C++ 做一版原生桌面端。**M1–M5 已全部完成，当前版本 v1.0.10，可日常使用。**

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
- [x] emoji 标题显示降级（最初替换成音符图标；v1.0.x 已升级为彩色 emoji，见下）

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
- [x] 网络缓存 20s（后可配置 2/5/10/20s，默认 5）
- [x] 扫码登录（qrcodegen 编码 + poll 轮询 + Set-Cookie 导入）

## M3 · 歌单导入与管理（已完成）

- [x] 收藏夹列表 / 稍后再看 / UP主投稿 / 合集（season）导入，实时进度条
- [x] 歌单新建 / 重命名 / 删除 / 排序（重名规则对齐 Java 版：覆盖 / 自动重命名 (n) / 取消）
- [x] 曲目多选：复制到歌单 / 移动到歌单 / 批量删除（按 bvid+cid 去重）
- [x] 歌单封面：从视频封面选 / 本地图片 / 渐变占位；网络封面异步缓存
- [x] 离线缓存：下载当前曲目到 ~/.bilimusic/downloads/
- [x] 本地音乐扫描（mp3/flac/wav/m4a/aac/ogg/opus/wma/ape）

## M4 · 体验对齐（已完成）

- [x] 封面图（网络图异步缓存 + 自定义本地图，ContentScale.Crop 裁切）
- [x] 沉浸模式（自下而上滑入 / 壁纸模糊背景 / 真实频谱 / 全屏 / 滚轮音量 / 鼠标空闲隐藏）
- [x] 播放模式（顺序 / 列表循环 / 单曲循环 / 随机）
- [x] 系统托盘（图标 + 菜单；关闭收进托盘）
- [x] 全局媒体键 + Ctrl+Alt+Space/←/→/H
- [x] 窗口内快捷键：空格、←/→ 3s、[ / ]
- [x] 定时关闭、防熄屏
- [x] 输出设备选择（WASAPI/mmdevice 枚举 + 切换 + 持久化；VLC 枚举失败用 IMMDeviceEnumerator 兜底）
- [x] 播放队列面板（高亮当前、点击切歌）
- [x] 整首缓存 + 预取下一首（`~/.bilimusic/cache`，命中即本地播放）
- [x] 侧栏整体折叠 + 底部功能入口折叠

## M5 · 打包分发（已完成）

- [x] 内置 VLC（复用 Java 版 portable 的 vlc/ 目录：libvlc + libvlccore + plugins，369 文件 / 135MB）
- [x] 免安装 zip（60.6MB）+ **NSIS 安装向导**（32.1MB，MUI2 简体中文，按用户安装免 UAC）
    - 历史：早期用 7zSD.sfx + py7zr 写 LZMA1 载荷；自定义 SFX 曾被卡巴斯基删文件，已废弃并换成官方 NSIS
- [x] 体积/内存/启动时间对比 Java 版：启动 0.66–0.81s、Working Set 215–241MB（Java 版约 3.2s / 430MB）
- [x] exe VERSIONINFO / 安装器图标 / 注册表 DisplayVersion 统一来自 CMake VERSION

## v1.0.10（2026-11-07）

- [x] 修复输出设备切换无效：aout 模块先钉 mmdevice，起播后在活 player 上重设 + 回读重试
- [x] WASAPI 会话级校验（AppVolume::findSessionDeviceId），确认过的设备不重复核对
- [x] 输出设备弹窗显示「当前实际输出 + 已生效/未生效」

## v1.0.9（2026-11-07）

- [x] 粘贴框「添加到歌单」：BV / 链接 → 当前歌单（取 view 信息，bvid+cid 去重）

## v1.0.8（2026-11-06）

- [x] httpGet 增加 maxBytes（默认 8 MiB），音频缓存/下载放宽到 256 MiB —— 修复「播一半就切歌」
- [x] 升级时清理旧版截断缓存（标记文件 .cache-v2-complete）
- [x] ended 但位置远未到结尾 → 判定缓存损坏，删缓存 + 改走在线流续播（单曲只重试一次）
- [x] 自动切歌日志带上 pos / duration / ended，便于定位

## v1.0.7（2026-11-06）

- [x] view 接口解析 `pic`，VideoInfo 增加 coverUrl
- [x] 分享链接导入写入 coverUrl（新导入直接带封面）
- [x] 「从歌单视频选封面」新增「补全缺失封面」，回填老歌单的空封面（LibraryStore::setSongCover）

## v1.0.6（2026-11-06）

- [x] 分享链接改用 zlib 包装（对齐 Java Deflater），修复手机端链接导入失败
- [x] 解码同时兼容 zlib 与 raw（老链接不失效）
- [x] 同名歌单导入确认：覆盖 / 自动重命名「名字 (2)」/ 取消（分享链接同样适用）

## v1.0.5（2026-11-06）

- [x] 「导入歌单」加入「分享链接」入口（粘贴 bm:// 链接直接导入）
- [x] 导入弹窗按钮改为中性灰（比对话框底色淡一档），不再用品牌红

## v1.0.4（2026-11-06）

- [x] 歌单分享链接 `bm://pl/v1/…`（编解码与 Java 版 PlaylistShareCodec 一致，base64url + raw deflate）
- [x] 粘贴分享链接导入整个歌单（逐首取 view 信息，新建歌单，不复用同来源）
- [x] 下载前确认弹窗（设置里也能看到保存目录）
- [x] 歌单内搜索（标题 / UP 主子串过滤，参与播放顺序）
- [x] 引入 miniz（public domain）做 raw deflate/inflate，third_party/miniz

## v1.0.3（2026-11-06）

- [x] 单实例保护（CreateMutex + 命名事件把已有窗口唤到前台，托盘隐藏也能拉回）
- [x] 侧栏「设置」入口：版本、数据目录、打开数据目录、播放缓存设置
- [x] 应用内卸载：可选保留 / 删除歌单与登录（默认保留），延迟批处理 → 静默卸载

## v1.0.2（2026-11-06）

- [x] 曲目表滚动裁剪修复（行内容改画在子窗口 draw list，不再漏到表头）
- [x] 「时长」表头与数值右对齐
- [x] 歌单页「随机播放」按钮（首曲随机 + 洗牌）
- [x] 「播放全部」图标垂直居中
- [x] 随机播放改洗牌袋（一轮不重复、播完重洗）
- [x] 定时关闭改「本曲播完再停」
- [x] 播放中假死看门狗（12s 不动 → 重建 + 回位）
- [x] 输出设备切换后重载并回位
- [x] 顺序播放到末尾停止
- [x] 音量曲线对齐 Java（低音量段）
- [x] 播放模式记忆 `desktop.playmode`

## v1.0.x 增补（v1.0.0 之后）

- [x] 自动切歌（收进托盘也生效）：`App::tick()` 从 `draw()` 移出 + `VlcEngine::ended()`
- [x] 进度条大跨度 seek 不回弹：15s 预览保持 + 1.2s 重试 + 3s 硬重载
- [x] 彩色 emoji（EmojiAtlas，D2D/DWrite 光栅化 + 富文本混排）
- [x] 输出设备枚举兜底（WASAPI IMMDeviceEnumerator）
- [x] 侧栏「功能」折叠箭头 / 每行 ? 提示
- [x] 任务栏图标（AUMID + 数字图标 ID）
- [x] 整首缓存开关（`audio.full_cache`）

## 功能对等清单

| 能力 | Java 版 | C++ 版 | 状态 |
| --- | --- | --- | --- |
| 本地音乐播放 | 支持 | 支持 | ✅ |
| 在线播放（WBI 取流） | 支持 | 支持 | ✅ |
| 扫码/Cookie 登录 | 支持 | 支持 | ✅ |
| 四种来源导入歌单 | 支持 | 支持 | ✅ |
| 歌单重名处理 | 支持 | 支持 | ✅ |
| 离线缓存 | 支持 | 支持 | ✅ |
| 沉浸模式 / 封面 | 支持 | 支持 | ✅ |
| 歌词页 | 无 | 无 | —（两边都没做） |
| 托盘 / 全局快捷键 / 防熄屏 | 支持 | 支持 | ✅ |
| 单实例保护 | 支持（SingleInstance.kt） | **缺** | 待做 |
| 打包体积/内存/启动 | zip 132.6MB、setup 102.7MB、~430MB、~3.2s | zip 60.6MB、setup 32.1MB、215–241MB、0.66–0.81s | ✅ |

## 后续（按优先级）

- **P0** 文档保持真实（README / ROADMAP 与代码同步）；每次发版 bump CMake VERSION
- **P1** 单实例保护：`CreateMutex` + 激活已有窗口，避免安装版/开发版/多开抢数据
- **P1** seek 稳定性：改用 libVLC 媒体播放器事件（PositionChanged / EndReached）替代轮询兜底
- **P1** 输出设备：切换后做实际路由验证（WASAPI 会话/设备峰值），必要时重启 player 生效
- **P2** `App.cpp`（约 3300 行）拆成 PlayerController / PlaylistController / AuthController；补关键路径测试
- **P2** 统一富文本：队列/弹窗剩余 `TextUnformatted` 调用改用 Rich text
- **P3** 性能：封面/emoji 纹理缓存上限、日志轮转、启动预加载

## 技术决策记录

- **UI 用 Dear ImGui 而不是 Qt**：M1 阶段优先「零第三方 SDK、构建简单、立刻能迭代」。Qt 需要安装 Qt SDK（几 GB）或 vcpkg 编译数小时，对当前目标不划算。ImGui 外观可定制到接近深色音乐播放器，但复杂动画/富文本能力有限；如果后续需要在 UI 上投入更多，可以评估迁移到 Qt6（业务层与 UI 解耦）。
- **libVLC 动态加载（LoadLibrary + GetProcAddress）**：不依赖 VLC SDK 头文件/import lib，打包时只要带 libvlc.dll + libvlccore.dll + plugins 即可。
- **WASAPI 应用级音量而不是 VLC 音量**：VLC 的音量写入自带短斜坡，高频重写会周期性爆音；改用会话级音量后拖动即时生效，且不会被换曲重置。
- **数据格式与 Java 版共用**：playlists.json / settings.json 直接读写，两个版本可以来回切换。
- **MinGW 静态链接**：避免安装版还要附带 libstdc++/libgcc/winpthread 三个 DLL。
- **安装器用官方 NSIS，不用自制 SFX**：7zSD 自定义 SFX 会被杀软误报删文件；NSIS 3.10 的 MUI2 体积更小、可按用户安装、可静默安装。
