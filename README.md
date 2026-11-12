# bilimusic · C++ 桌面版 v1.1.5

> 这是 [bilimusic](https://github.com/amns56780/bili-music) 桌面端的 **C++ 重写版**，目前是本机日常使用的版本。
> 原来的 Kotlin / Compose 版本继续维护、不受影响；这里是并行的原生实现，两者共用同一份用户数据。

## 为什么另起一版

- 去掉对 **JRE/JDK 17+** 的依赖：Java 版安装包只内置 VLC，仍要求机器上有 Java；C++ 版是原生 exe，无运行时依赖
- 更小的体积和内存占用、更快的启动（Java 版实测启动约 3.2s、Working Set 约 430MB；C++ 版 0.66–0.81s、215–241MB）
- 顺带把播放内核（libVLC）的封装换成直接 C API（`LoadLibrary` + `GetProcAddress`），不再经过 VLCJ/JNA

## 当前状态

**M1–M5 全部完成，版本 v1.1.5，可日常使用。**

> 接手这个项目请先读 **[docs/HANDOVER-AI.md](docs/HANDOVER-AI.md)** —— 环境 / 发版流程 / 架构地图 / 六个已修 bug 的根因 / 红线清单 / 调试手册全在里面。
> 项目根目录已迁到 **F:/BiliMusic**（2026-11-08），搬家与清理记录见 [../MIGRATION-2026-11-08.md](../MIGRATION-2026-11-08.md)。

| 里程碑 | 内容 | 状态 |
| --- | --- | --- |
| M1 / M1.5 | CMake + MinGW 构建、无边框自绘 UI、侧栏/歌单页/播放条、读取 Java 版数据、libVLC 播放 | ✅ |
| M1.6 | WASAPI 应用级音量、Win32 原生拖动/缩放、圆角抗锯齿、音量条重置修复 | ✅ |
| M2 | 扫码/Cookie 登录、WBI 签名、取流与音质档位、在线播放、备用地址、看门狗 | ✅ |
| M3 | 歌单增删改/重名处理、四种来源导入、多选复制/移动/删除、封面三种来源、下载、本地扫描 | ✅ |
| M4 | 托盘、全局媒体键、窗口内快捷键、沉浸模式、队列、定时、输出设备、防熄屏、整首缓存+预取 | ✅ |
| M5 | 内置 VLC、portable zip、NSIS 安装向导 | ✅ |

## 功能明细

### 基础与 UI

- CMake + MinGW-w64 构建，依赖全部内嵌在 `third_party/`（ImGui / GLFW / nlohmann-json / qrcodegen / stb），不需要 Qt、vcpkg、Visual Studio
- 无边框自绘窗口：标题栏 `WM_NCHITTEST` 返回 `HTCAPTION` 交系统 modal 拖动，四边/四角返回 `HT*` 原生缩放，`WM_GETMINMAXINFO` 最大化不盖任务栏
- 深色主题（`ui/Theme`）、Material Icons 合并进正文、微软雅黑 Bold、行高与表格同步
- **彩色 emoji**：`ui/EmojiAtlas` 用 WIC + Direct2D + DirectWrite 把 Segoe UI Emoji 光栅化成 GL 纹理，富文本路径（`buildRichItems` / `drawRichTextImpl`）按字形混排彩色 emoji 与图标
- 侧栏可整体折叠（标题栏箭头 / `Ctrl+B`，带宽度动画、状态记忆）；底部「功能」入口可单独折叠；每行右侧 `?` 按钮带说明 tooltip

### 登录与在线播放

- Cookie 登录（读写与 Java 版共用的 `settings.json` 的 `cookie_blob`）+ 扫码登录（`qrcode/generate` + `qrcode/poll`，内置 qrcodegen 编码、ImGui 直接画）
- WBI 签名：nav 取 `img_url`/`sub_url` → 重排表得 mixin_key（缓存 24h）→ 参数加 `wts` 排序 → MD5 得 `w_rid`
- `x/web-interface/view` 取 cid/标题/UP主/时长；`x/player/wbi/playurl`（fnval=4048）取流，失败强刷 WBI key 重试，再退 `x/player/playurl`
- 音质档位：自动 / 192K / 132K / 64K / Hi-Res / 杜比，写入 `audio_quality`（与 Java 版共用）；降级链 + 最高带宽兜底 + durl 兜底
- 在线播放带 `http-referrer` / `http-user-agent`（否则 bilivideo 403）；备用地址切换 + 6s 看门狗强制重取流
- 网络缓存可配置（2/5/10/20 秒，默认 5，历史默认 20）
- 音量走 WASAPI `ISimpleAudioVolume`（不经过 VLC），写入节流 + 会话过期重建检测，避免「一炸一炸」

### 歌单管理

- `data/LibraryStore` 读写 `playlists.json`（与 Java 版同格式，临时文件 + 改名安全落盘）
- 新建 / 重命名 / 删除 / 排序；重名处理（覆盖 / 自动 `(n)` / 取消），规则与 Java 版 `PlaylistNaming` 一致
- 导入收藏夹 / 稍后再看 / UP 主投稿 / 合集系列，实时进度；同来源重复导入复用原歌单
- 多选：全选 / 清空 / 复制 / 移动（按 bvid+cid 去重）/ 批量删除
- 封面三种来源：从歌单视频选（缩略图列表 + 大图预览）/ 本地图片（复制进 `covers/`，stb_image + GL 纹理）/ 网络封面异步缓存
- 下载当前曲目到 `~/.bilimusic/downloads`；本地音乐扫描（mp3/flac/wav/m4a/aac/ogg/opus/wma/ape）

### 体验

- 系统托盘：显示 / 播放暂停 / 上一首 / 下一首 / 退出；关闭按钮收进托盘，托盘退出才真退出
- 全局键盘钩子：媒体键 + `Ctrl+Alt+Space` / `←` / `→` + `Ctrl+Alt+H`（窗口隐藏也生效）
- 窗口内快捷键：空格、`←`/`→` 快退快进 3s、`[`/`]` 上一首/下一首（沉浸模式同）
- 沉浸模式：点播放条封面自下而上滑入、桌面壁纸高斯模糊背景、全屏按钮、真实频谱（WASAPI loopback + 1024 点 FFT）、鼠标 3s 空闲隐藏、滚轮 ±1% 音量、`Esc` 关闭
- 播放队列面板、定时关闭（15–120 分钟）、输出设备选择、防熄屏（`SetThreadExecutionState`）
- **整首缓存 + 预取下一首**：后台把整首音频下载到 `~/.bilimusic/cache`，完成后切本地播放（seek 秒响应），并预取紧随其后的一首；侧栏「播放缓存」可开关整首缓存、立即缓存、清除缓存
- 窗口位置/大小 + 音量记忆；空闲时播放条显示 BM 图标

### 分发

- 内置 VLC（`libvlc` + `libvlccore` + 369 个 plugins，约 135MB）
- 免安装 zip（60.6MB）+ NSIS 安装向导（32.1MB，Modern UI 2 简体中文，按用户安装免 UAC，可选桌面快捷方式，注册卸载项）
- 实测：启动 **0.66–0.81s**、Working Set **215–241MB**、exe **5.65MB**

## 版本号规则

`major.minor.patch`，**逢十进一**：patch 走到 9 之后进位到 minor（`1.1.9 → 1.2.0`），minor 到 9 之后进位到 major。
单一来源是 `CMakeLists.txt` 里的 `project(bilimusic_cpp VERSION x.y.z)`，安装包、exe 资源、关于页都从它取值。

## v1.1.5 新增（沉浸输出设备 / 拖动排序 / 框选多选）

- **沉浸模式输出设备**：右上角新增耳机按钮与设备弹窗，切换后立即生效并从当前位置续播
- **点音量面板外关闭**：音量条不用再点一次音量按钮才能收起
- **音律条跟随设备**：频谱抓的是 App 当前选中的输出设备（不是系统默认设备），切换设备后自动重建 WASAPI loopback
- **沉浸输出设备弹窗**：圆角面板 + 内边距，标题 / 提示 / 列表 / 按钮分层，选中设备用品牌色高亮
- **拖动排序**：歌单列表、歌单内曲目都可按住拖动；拖起行变亮 + 上下拖动光标，拖到列表上下边缘自动滚动；写回 `playlists.json`（歌单数组顺序 / 曲目 sortOrder），支持 Ctrl+Z 撤销
- **框选多选**：多选模式下按住拖动框选，行为类似 Windows 资源管理器
- 修复：曲目拖拽只写 sortOrder、内存没重排导致「拖了界面不变」的问题（现在即时生效）

> 本版包含 v1.1.4 的「换曲瞬间音量突然变大」修复。

## v1.1.4 修复（换曲瞬间音量突然变大）

自动切歌时，新曲开头偶尔会突然变响、随后又回到设置的音量。根因：`pumpVolume()` 以前每 250ms 才回读一次 WASAPI 会话音量，而系统在换曲时会把会话音量重置回 100%，最长 250ms 之后才被写回。现在改成**每帧回读、只在真不一致时写**（写才会产生可闻跳变），会话重试节流 500ms → 100ms，起播/换曲时清零。实测：把会话音量强制改成 100% 后 50ms 内纠回（修复前 220ms）。

## v1.1.3 新增（撤销）

- **Ctrl+Z 撤销上一步**：新建/重命名/删除歌单、删除曲目、导入歌单、添加单曲、扫描本地音乐、复制/移动曲目、更换封面 —— 每次操作前打一份内存快照（最多 20 步），Ctrl+Z 回退；正在输入文字时让给输入框自己的撤销
- 状态栏提示「已撤销：<操作名>」，日志也记一条

## v1.1.2 修复（扫描本地音乐写错歌单）

「扫描本地音乐文件夹」以前**永远写进「本地音乐」**（代码里写死了 `findBySource("LOCAL","本地音乐")`），在别的歌单里点扫描就会觉得"一首都没进来"。现在扫到的曲目进**当前选中的歌单**，状态栏会写明「新增 N 首到「歌单名」」。
另外支持 `BILIMUSIC_SCAN_DIR=<目录>` 环境变量直接指定扫描目录（跳过文件夹选择框，自检/脚本用）。

## v1.1.1 修复（输出设备真的换了）

v1.0.10/v1.1.0 那版的修法不够——实测**同一进程里一旦建过 aout，之后无论 `device_set` 还是 media 选项都会被 VLC 3 忽略**（设备只在 aout 创建时读一次）。
所以改成「按新设备重建整个 libvlc 实例（`--mmdevice-audio-device=<id>`）+ 重新起播」，并用 WASAPI 会话所在设备逐秒核对：

```
13:29:56 设备观察：会话现在挂 {…7970f42f…}（目标）
13:29:59 输出设备核对：请求={…7970f42f…} 会话实际={…7970f42f…} ✅ 已生效
```

- 启动时读到的已保存设备也会先重建实例再起播（之前是先 init 再设，等于没设）
- 「输出设备」弹窗显示「当前实际输出：xxx（已生效 / 未生效，正在重试）」

## v1.1.0（= 原 v1.0.10，同一份内容，改为逢十进一编号）

- 修复输出设备切换无效（见下）

## v1.0.10 修复（输出设备切换无效）

- **切了输出设备但声音还从旧设备出**：VLC 3 在 player 还没起播（没有 aout）时设设备会被静默丢弃 —— 起播前设的那次等于没设，
  于是每次都回落到系统默认设备。现在先 `libvlc_audio_output_set("mmdevice")` 钉住模块，起播后再**在已起播的 player 上重设一次**并回读确认（最多重试 2 次）
- 校验方式换成 WASAPI：枚举所有 render endpoint，看本进程音频会话实际挂在哪个设备上（VLC 3.0 没有回读 API），确认过的设备不再重复核对
- 「输出设备」弹窗新增一行 **当前实际输出：xxx（已生效 / 未生效，正在重试）**，不再只是显示"你选了什么"

## v1.0.9 新增

- **往歌单里加单曲**：粘贴框新增「添加到歌单」按钮 —— 粘贴 BV 号 / 视频链接后直接加进**当前歌单**（不播放），
  自动取标题 / UP主 / 时长 / 封面，按 (bvid, cid) 去重，重复提示「已经在这个歌单里了」；弹窗底部会显示目标歌单名

## v1.0.8 修复（重要）

- **歌播到一半自动切歌**：`net::httpGet` 里有一行写死的 8 MiB 上限（`body.size() > 8*1024*1024 → break`），
  整首缓存和「下载歌曲」都走它 —— 超过 8 MiB 的歌（Hi-Res/无损/较长曲目）只存了前一半，VLC 播到文件尾就 Ended，于是自动切下一首。
  现在 `httpGet` 增加 `maxBytes` 参数（JSON 接口仍 8 MiB），音频缓存/下载用 256 MiB
- 升级后一次性清理旧的截断缓存（写 `.cache-v2-complete` 标记），让它们按完整大小重新缓存
- 兜底：ended 但位置离总时长还差 3 秒以上 → 判定缓存损坏，删除并改走在线流续播（同一首只重试一次）

## v1.0.7 修复

- **分享导入的歌单没有视频封面**：`BiliService::fetchVideo`（view 接口）之前没解析 `data.pic`，`VideoInfo` 也没有封面字段 → 分享导入写进库的 `coverUrl` 全为空，列表/侧栏/封面选择弹窗都渲染成占位（重启也不会自己长出来）。现在 view 解析 `pic` 并在分享导入时写入
- 老歌单补全入口：「从歌单视频选封面」里新增 **补全缺失封面**，对缺 `coverUrl` 的曲目逐首取一次 view 后回写（26 首约 3 秒）

## v1.0.6 修复

- **手机端 / Java 侧分享链接解不开**：Java 的 `Deflater(int)` 产出的是 **zlib 包装**（`78 9c / 78 da` 头 + adler32 尾），不是 raw deflate；C++ 之前按 raw 解会直接失败。现在编码改成 zlib 包装（与 Java 完全一致），解码优先 zlib、失败再按 raw 兜底（兼容早期 C++ 生成的链接）
- 同名歌单导入：沿用「覆盖 / 自动重命名（Windows 风格 `名字 (2)`）/ 取消」三选一，分享链接导入同样生效

## v1.0.5 新增

- 「导入歌单」弹窗新增 **分享链接** 入口（收藏夹 / 稍后再看 / UP主投稿 / 合集 / 分享链接），点开输入框粘贴 `bm://pl/v1/…` 即导入
- 该弹窗的按钮从品牌红改为**比对话框底色略淡一档的灰**（#2A2A31 / hover #35353C / active #3C3C44），不再抢视线

## v1.0.4 新增

- **歌单分享 / 导入（与 Java 版同一协议）**：歌单「…」→「分享歌单」生成 `bm://pl/v1/<base64url(deflate(JSON))>` 并复制到剪贴板；
  在「粘贴链接」里粘贴该链接即导入整个歌单（逐首取视频信息后新建歌单）。协议与 Android 端互通，不经过任何服务器
- **下载前确认**：点下载会先弹确认框，显示曲目名与保存目录，确认后才真正落盘
- **歌单内搜索**：歌单页右上角搜索框，按标题 / UP 主实时过滤；过滤结果参与「播放全部 / 随机播放 / 下一首」的顺序

## v1.0.3 新增

- **单实例保护**：重复启动只保留一个进程；第二个实例会把已有窗口唤到前台（收进托盘时也会拉回来）后自行退出，避免两个窗口共用同一份数据互相覆盖设置
- **应用内「设置」**：侧栏「功能」新增入口，显示版本与数据目录，可一键打开数据目录、跳转播放缓存设置
- **应用内卸载**：设置里的「卸载 bilimusic…」，可选择**是否保留歌单与登录信息**（默认保留）；确认后程序退出并静默调用自带卸载器，安装目录与快捷方式一并清理

## v1.0.2 新增

### 界面

- **曲目表滚动裁剪修复**：行内容（时长 / 序号 / 高亮 / 勾选框）改为画在列表子窗口自己的 draw list 上，滚出视区的行不再漏到表头上方；「时长」表头与数值右对齐
- **歌单页新增「随机播放」**：位于「播放全部」与「多选」之间；点击后切随机模式，**首曲从整个歌单随机挑**，再按洗牌顺序继续（不是固定从第一首开始）
- 「播放全部」的 ▶ 图标下移 2px，与文字视觉居中

### 播放逻辑（对齐 Java 版）

- **随机播放改用洗牌袋**：替换 `rand() % count`，一轮内不重复；一轮播完把刚播完的排到最后再重洗；上一首按洗牌顺序回退
- **定时关闭改为「本曲播完再停」**：到点只置标记，不再立刻掐断（Java 默认语义）；取消定时会清掉标记
- **播放中假死看门狗**：2s 采样、位置连续 12s 不前进 → 重建媒体并回到原位置，最多 3 次（原来只有「起播 6s」那一段）
- **输出设备切换后重载并回位**：VLC 3 只在媒体启动时读设备，切换后重载当前曲目并 seek 回原位置
- **顺序播放到末尾停止**：不再回绕到第一首
- **音量曲线对齐 Java**：`1..10 → 0.001u`、`11..99 → 0.01+0.99·((u-10)/90)^1.1`，低音量段不再比 Java 版响 10 倍
- 播放模式记忆：写入 `desktop.playmode`

## v1.0.1 相比 v1.0.0 的修复

| 内容 | 关键实现 |
| --- | --- |
| 后台自动切歌（收进托盘也生效） | `App::tick()` 移出 `draw()`，主循环每帧调用；`checkTrackEnd()` 判 `VlcEngine::ended()` 或位置接近总时长且非用户暂停；`autoNextHandledSongId_` 防重复 |
| 进度条大跨度 seek 回弹 | `requestSeekPreview()` 预览保持 15s；1.2s 未动重试；3s 未动重载媒体；失败提示「这个位置暂时不能跳转」 |
| 彩色 emoji | `ui/EmojiAtlas`（D2D/DWrite 光栅化）+ `Components` 富文本，`TextEllipsis`/`drawTextAt` 走富文本 |
| 输出设备枚举兜底 | `VlcEngine::audioOutputDevices()` 在 VLC 枚举失败时用 `IMMDeviceEnumerator` 兜底；`applyOutputDevice()` 写 mmdevice endpoint id |
| 侧栏折叠 + 每行 `?` 提示 | `Sidebar` 的 `actionsExpanded` 分支与 Info 按钮；状态存 `ui.sidebar_actions_expanded` |
| 任务栏图标 / AUMID | 数字图标 ID + `SetCurrentProcessExplicitAppUserModelID` |
| NSIS 现代化安装器 | 替换 7zSD SFX（自定义 SFX 曾被卡巴斯基删文件），MUI2 + 中文 + 品牌图 |
| 整首缓存开关 | `audio.full_cache` |

## 构建

要求：CMake 3.20+、MinGW-w64（w64devkit，g++ 13+）。**不需要 Qt / vcpkg / Visual Studio。**

> 构建前先结束正在运行的实例，否则链接会报 `cannot open output file bilimusic.exe: Permission denied`。

```powershell
Get-Process bilimusic -ErrorAction SilentlyContinue | Stop-Process -Force

# 用仓库自带的脚本（自动找 ../tools 下便携版 MinGW + CMake）
.\scripts\build.ps1            # Release（GUI 子系统）
.\scripts\build.ps1 -Console   # 控制台版，方便看 stderr / VLC 日志
.\scripts\build.ps1 -Clean     # 清理后重建

# 或者手动
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

产物：`build\bilimusic.exe`、`build\assets\MaterialIcons-Regular.ttf`。

运行时查找 VLC 的顺序（`VlcEngine::init`）：`BILIMUSIC_VLC_DIR` → `<exe目录>\vlc\libvlc.dll` → `<exe目录>\libvlc.dll` → `C:\Program Files\VideoLAN\VLC`。
开发版旁边没有 `vlc/`，会回退系统 VLC；安装版用自带内核。

自检入口：

| 命令 | 用途 |
| --- | --- |
| `bilimusic.exe --test-pause <本地音频>` | WASAPI 峰值采样验证暂停/恢复不断音 |
| `bilimusic.exe --test-pause-online <BV号>` | 同上，走在线流 |
| `bilimusic.exe --check-bili <BV号>` | 控制台自检：nav 登录态 / view 取 cid / playurl 取流 |
| `bilimusic.exe --qr-test` | 扫码登录自检 |

环境变量：`BILIMUSIC_RESUME_PLAY`、`BILIMUSIC_TEST_NO_VOLUME`、`BILIMUSIC_VLC_VERBOSE`。

## 打包

需要 NSIS 3.x（解压 portable 版到 `../tools/nsis`，或设置 `NSIS_DIR`；不在仓库里）。

```powershell
Get-Process bilimusic -ErrorAction SilentlyContinue | Stop-Process -Force
.\scripts\package.ps1              # 构建 + 打 zip + 打安装器 + 自检 + 冒烟测试
.\scripts\package.ps1 -SkipBuild   # 复用现有 build 产物
.\scripts\package.ps1 -SkipRuntime # 跳过冒烟测试
```

| 产物 | 说明 |
| --- | --- |
| `build\dist\bilimusic-cpp-portable-win64.zip` | 免安装包（解压即用，内置 `vlc/`，约 60.6MB） |
| `build\dist\bilimusic-cpp-setup.exe` | NSIS 安装向导（MUI2 简体中文），按用户安装免 UAC，默认 `%LOCALAPPDATA%\Programs\bilimusic`，开始菜单 + 可选桌面快捷方式 + HKCU 卸载项，支持 `/S` 静默安装，约 32.1MB |

实现说明：

- 安装器脚本 `scripts/packaging/bilimusic.nsi`（**必须保留 UTF-8 BOM**，否则 makensis 报 "Bad text encoding"）；品牌图 `installer-welcome.bmp`（164x314）/ `installer-header.bmp`（150x57）
- 载荷由 NSIS `File /r` 打进 setup.exe（SOLID LZMA）；免安装 zip 用 PowerShell `Compress-Archive` 生成
- 版本号单一来源：`CMakeLists.txt` 的 `project(... VERSION x.y.z)` 同时进 `BILIMUSIC_VERSION` 宏、exe VERSIONINFO、安装器与注册表 `DisplayVersion`
- 静默安装：`bilimusic-cpp-setup.exe /S /D=D:\bilimusic`（`/D` 必须最后且不带引号）；静默卸载 `D:\bilimusic\uninstall.exe /S`
- 未做数字签名，SmartScreen 会提示「未知发布者」；**不要**为了消提示改回自制 SFX（会被杀软针对）

## 目录结构

```
src/
  main.cpp                    GLFW + ImGui 生命周期；主循环调用 app.tick()；自检入口
  app/App.{h,cpp}             应用状态机 + 动作分发
  ui/
    Theme.{h,cpp}             颜色/尺寸/字体（对应 Java 版 BmTheme.kt）
    Components.{h,cpp}        控件库 + 富文本绘制（emoji 纹理）
    Icons.h                   Material Icons 码点
    TitleBar / WindowFrame    自绘标题栏 + Win32 原生拖动/缩放
    Sidebar.{h,cpp}           侧栏（折叠 + 底部功能入口 + 说明提示）
    PlaylistPage.{h,cpp}      歌单页 + 曲目表 + 多选 + 菜单
    PlayBar.{h,cpp}           底部播放条
    ImageCache.{h,cpp}        封面下载 + GL 纹理 + 桌面壁纸高斯模糊
    EmojiAtlas.{h,cpp}        彩色 emoji 纹理缓存（D2D/DWrite）
    AppIcon.h                 内嵌多尺寸 BM 图标
  media/
    VlcEngine.{h,cpp}         libVLC 封装：播放/暂停/seek/音量/输出设备/ended()
    AppVolume.{h,cpp}         WASAPI 应用级音量（+ loopback 峰值）
    AudioSpectrum.{h,cpp}     loopback 采集 + 1024 FFT + 32 对数频段
  bili/BiliService.{h,cpp}    QR 登录、WBI、取流、歌单导入、下载
  data/LibraryStore.{h,cpp}   playlists.json 读写（Java 同格式）
  net/HttpClient.{h,cpp}      WinHTTP 封装
  platform/SystemTray.{h,cpp} 托盘图标 + 全局键盘钩子
  util/{AppLog,Md5,QrCode}    日志 / MD5 / 二维码矩阵
  resources/app.rc.in         exe 图标（数字 ID 1）+ VERSIONINFO
third_party/                  imgui / glfw / nlohmann / icons / qrcodegen / stb
scripts/                      build.ps1 / package.ps1 / packaging/
```

## 数据目录（与 Java 版共用）

`%USERPROFILE%\.bilimusic\`：`playlists.json`（歌单/曲目）、`settings.json`（Cookie/音量/窗口/缓存/输出设备/侧栏状态）、`cache/`（整首缓存）、`downloads/`、`covers/`、`cpp-app.log`。

两个版本**不要同时运行**：没有单实例锁，窗口几何/音量/Cookie/歌单会互相覆盖。

## 和 Java 版的关系

- `playlists.json` / `settings.json` 格式完全兼容，两个版本可来回切换、共用数据目录
- 本机默认使用 C++ 版（`D:\bilimusic`）；Java 桌面版作为参考/回退，`source/` 供行为对照
- C++ 侧的自动切歌 / seek / emoji / NSIS 等修复不会自动回流 Java（除非明确要求）
- Android 端不受影响，按 Java 仓库继续维护

## 已知限制

- 没有单实例保护（`CreateMutex` 待加），多开会抢数据
- 大跨度 seek 在网络流上仍可能慢：预览最多 15s，1.2s 重试、3s 硬重载；VLC 完全不可 seek 时只提示不重试
- 输出设备切换只做了界面与持久化验证，未做声学路由验证
- 彩色 emoji 只覆盖富文本路径；队列/弹窗里仍是 Material 图标占位
- 未实现：全局搜索页、排行榜、歌词页（Java 版也没有歌词页）
- 无代码签名、无自动更新；`cpp-app.log` 无轮转

## 依赖与许可

| 依赖 | 版本 | 许可 |
| --- | --- | --- |
| Dear ImGui | 1.93.0 | MIT |
| GLFW | 3.5.1 | zlib/libpng |
| nlohmann/json | 3.12.0 | MIT |
| qrcodegen (Nayuki) | 1.8.0 | MIT |
| stb_image | 2.30 | public domain / MIT |
| libVLC | 3.0.21（运行时） | LGPL/GPL |

本项目代码：MIT（见 `LICENSE`）。
