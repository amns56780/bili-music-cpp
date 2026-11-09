# bilimusic · C++ 桌面版 —— AI 交接文档

> 生成时间：2026-11-08 ｜ 当前版本：**v1.1.3**
> 交接范围：F:/BiliMusic 下的 cpp/（主用，本机日常播放器）与 source/（Kotlin/Compose 桌面 + Android，参考实现）
> 姊妹文档：source/HANDOVER-AI.md（Java/Kotlin 版交接，含两端共用数据与分享码契约）
> 迁移记录：项目根目录 2026-11-08 从 C 盘迁到 **F:/BiliMusic**，清理与验证见 ../MIGRATION-2026-11-08.md（本文内路径已同步更新）
> 仓库：https://github.com/amns56780/bili-music-cpp
> 说明：本文路径统一用 / 分隔（Windows 资源管理器里等价）。

---

## 0. 三十秒速览

- cpp/ 是一个 **C++20 + Dear ImGui + GLFW + libVLC 3.0.21** 的 B 站音频播放器（Windows 桌面、自绘 UI，无 Qt / 无 Electron）。约 45 个源文件、1.9 万行。
- 已到"能当日常播放器"：登录（扫码 + Cookie）、收藏夹 / 稍后再看 / UP主投稿 / 合集导入、本地音乐扫描、在线播放（含 Hi-Res 降级链）、整首缓存 + 预取、seek、随机（洗牌袋）/单曲/列表/顺序、定时关闭、系统托盘、单实例、应用内设置与卸载、歌单分享码 bm://pl/v1/...（与 Java/Android 互通）、封面选择与补全、Ctrl+Z 撤销。
- 产物：bilimusic-cpp-setup.exe（NSIS MUI2，按用户安装、免 UAC、内置 VLC）与 bilimusic-cpp-portable-win64.zip；发版到 GitHub Releases。
- 安装位置：D:/bilimusic；用户数据：C:/Users/amns2333/.bilimusic（**真实数据，动前先备份**）。
- 最近三天主要在修 bug：分享码编码、导入封面、缓存截断、输出设备、撤销。根因与实测证据都在第 6 节，**别重走弯路**。

---

## 1. 仓库结构

    F:/BiliMusic/
    ├─ cpp/                        ← 主项目
    │  ├─ CMakeLists.txt           ← 版本单一来源 project(bilimusic_cpp VERSION x.y.z)
    │  ├─ src/
    │  │  ├─ app/App.{h,cpp}       ← 约 4.3k 行，业务逻辑 / 弹窗 / 快捷键（God object）
    │  │  ├─ bili/BiliService.*    ← B 站 API：登录、收藏夹、UP主、合集、view、playurl、分享码
    │  │  ├─ data/LibraryStore.*   ← playlists.json 读写、命名规则、快照（撤销）
    │  │  ├─ media/VlcEngine.*     ← libvlc 动态加载（LoadLibrary + GetProcAddress），播放/音量/输出设备
    │  │  ├─ media/AppVolume.*     ← WASAPI 应用级音量 + 会话设备查询（诊断）
    │  │  ├─ media/AudioSpectrum.* ← 真实频谱
    │  │  ├─ net/HttpClient.*      ← WinHTTP 封装（注意 maxBytes，第 7 节坑 1）
    │  │  ├─ platform/SystemTray.* ← 托盘
    │  │  ├─ ui/                   ← Sidebar / PlaylistPage / PlayBar / TitleBar / Components / Theme / Icons / EmojiAtlas / ImageCache / WindowFrame
    │  │  └─ util/                 ← AppLog、ShareLink、QrCode、Md5
    │  ├─ docs/                    ← ROADMAP.md、HANDOVER.md（旧）、HANDOVER-AI.md（本文）
    │  ├─ scripts/                 ← build.ps1、package.ps1、run.ps1、packaging/bilimusic.nsi
    │  └─ third_party/             ← imgui、glfw、nlohmann、miniz、stb、qrcodegen、icons
    ├─ source/                     ← Kotlin 版（desktop + Android），只作行为参考
    ├─ tools/                      ← w64devkit、cmake、nsis、7zip、截图脚本、临时测试程序
    └─ HANDOVER-CPP.md             ← 旧交接稿（保留作历史）

source/ 仓库**历史已分叉**（本地 ahead 73 / behind 76，merge-base 780e073）：**不要 force push、不要尝试合并**，只当参考实现读（同名冲突规则、随机播放语义、音量曲线等）。

---

## 2. 环境与命令速查

| 用途 | 命令 |
|---|---|
| 构建 | cd cpp -> powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 |
| 带控制台构建 | scripts/build.ps1 -Console |
| 干净重建 | scripts/build.ps1 -Clean |
| 运行 | cpp/build/bilimusic.exe（或 scripts/run.ps1） |
| 打包（zip + 安装器 + 冒烟测试） | scripts/package.ps1（可加 -SkipBuild / -SkipRuntime） |
| 静默安装到 D 盘 | cpp/build/dist/bilimusic-cpp-setup.exe /S "/D=D:/bilimusic" |
| 查版本 | powershell -c "(Get-ItemProperty 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/bilimusic').DisplayVersion" |
| 日志 | C:/Users/amns2333/.bilimusic/cpp-app.log |

工具链固定路径（脚本内写死，可用环境变量覆盖）：

- MinGW-w64：F:/BiliMusic/tools/w64devkit（W64DEVKIT）
- CMake：F:/BiliMusic/tools/cmake（CMAKE_DIR）
- NSIS 3.x：F:/BiliMusic/tools/nsis（NSIS_DIR）
- 打包时 VLC 运行时来源：优先 source/desktop/build/portable/bilimusic/vlc，退 tools/portable-test/vlc（package.ps1 的 vlcCandidates）

**PowerShell 脚本必须纯 ASCII、不带 BOM**：Windows PowerShell 5.1 会把无 BOM 的 .ps1 当 ANSI 解析，中文注释会炸。

---

## 3. 版本规则与发版流程

### 3.1 版本号规则（用户明确要求）

major.minor.patch，**逢十进一**：1.1.9 -> 1.2.0，1.9.9 -> 2.0.0。第 10 个补丁不要叫 1.0.10，直接进位（v1.0.10 就是这么变成 v1.1.0 的，旧 tag/release 已删）。

**唯一来源**：cpp/CMakeLists.txt 的 project(bilimusic_cpp VERSION x.y.z)。exe 资源版本、安装器 DisplayVersion、关于页都从这里取（package.ps1 用正则读）。

### 3.2 一次完整发版

    1) 改版本：cpp/CMakeLists.txt / cpp/README.md 标题与版本行 / cpp/docs/ROADMAP.md 加一条
       写 tools/release-notes-vX.Y.Z.md
    2) 打包：cd cpp && powershell -NoProfile -ExecutionPolicy Bypass -File ./scripts/package.ps1
    3) 安装：./build/dist/bilimusic-cpp-setup.exe /S "/D=D:/bilimusic"  然后核对 DisplayVersion
    4) 校验和：sha256sum build/dist/bilimusic-cpp-setup.exe build/dist/bilimusic-cpp-portable-win64.zip
       填进 release notes 的 SETUP_SHA / ZIP_SHA 占位
    5) 提交：git add -A && git commit -m "release: vX.Y.Z —— 一句话"
             git tag -a vX.Y.Z -m "..."
             git -c credential.helper='!gh auth git-credential' push origin main
             git -c credential.helper='!gh auth git-credential' push origin vX.Y.Z
    6) 发布：export HTTPS_PROXY=http://127.0.0.1:65532 HTTP_PROXY=http://127.0.0.1:65532
             gh release create vX.Y.Z --repo amns56780/bili-music-cpp \
               --title "bilimusic vX.Y.Z（C++ 桌面版）" --notes-file tools/release-notes-vX.Y.Z.md \
               build/dist/bilimusic-cpp-setup.exe build/dist/bilimusic-cpp-portable-win64.zip

release notes 固定结构：本版修复/新增 -> 根因与实测日志 -> 下载表 -> SHA256 两行。

### 3.3 git 推送的两个硬坑

1. 没有存储 git 凭据：直接 git push 会挂在 "could not read Username"，必须
   git -c credential.helper='!gh auth git-credential' push origin main。
2. GitHub 直连不通：gh 命令必须带 HTTPS_PROXY=http://127.0.0.1:65532（git 走 helper 可以不用代理）。
   曾因漏加 helper 让后台任务挂住 10 分钟，只能 job_kill。

---

## 4. 架构地图与关键链路

### 4.1 App 的职责（App.cpp）

- 启动：App::init() —— 加载 store、VLC、设置；**读 audio.outputDevice 后要重建 VLC 实例**（见 6.4）。
- 每帧：App::tick() -> syncPlaybackState()（位置/时长/音量/设备核对）、checkTrackEnd()、checkPlaybackStall()、checkSleepTimer()、pollAsync()。
- 异步：postAsync(AsyncEvent) + pollAsync()，全部后台 std::thread + 事件队列。Kind 枚举：Play / BvPlay / ImportDone / ImportProgress / DownloadDone / LocalScanDone / Login / QrGenerate / QrPoll / CacheReady / Prefetched / Covers / AddSong。
- 播放：startOnlinePlayFor()（核心，约 100 行）决定"整首缓存命中 -> 本地播放 / 命中流地址 -> playUrl / 重新解析 -> 起播"；stepSong(direction) 管上/下一首；playShuffleStep() 是洗牌袋。
- 导入：ImportContext + startImportWorker()（后台取数据）-> finishImport()（落库 / 冲突弹窗）；handleConflictChoice() 处理"覆盖 / 自动重命名 / 取消"。
- 分享码：shareCurrentPlaylist()（编码 + 剪贴板 + 弹窗）、startShareLinkImport()（解码 + 逐首 view 补信息 + 落库，forceNew=true）。
- 撤销：pushUndo(label) / undoLastOperation()，快照来自 LibraryStore::Snapshot。

### 4.2 播放链路（排障必看）

    双击曲目 -> playSongFromUser(id) -> playSongById(id)
             -> startOnlinePlayFor(songId, bvid, cid, title, artist)
                  ├─ 整首缓存命中(cacheFileUsable) -> engine_.playPath(cache)（含时长完整性校验）
                  ├─ 未命中 -> engine_.playUrl(流地址, Referer/UA) + startCacheDownload()
                  └─ 预取下一首 -> prefetchNextSongCache()
    VLC 状态 -> syncPlaybackState() -> checkTrackEnd()（ended / nearEnd）-> stepSong(1)

音量不经过 VLC：AppVolume（WASAPI ISimpleAudioVolume）直接写应用会话；VlcEngine::pumpVolume() 每帧自愈（VLC 换曲会把会话音量重置）。

### 4.3 数据落盘

LibraryStore::save() 写 C:/Users/amns2333/.bilimusic/playlists.json，**与 Java 版共用**：字段名与可空写法必须保持一致，否则两端互相踩。

---

## 5. 这几天交付了什么（v1.0.1 -> v1.1.3）

| 版本 | 主题 | 关键内容 |
|---|---|---|
| v1.0.1 | 文档对齐 | README / ROADMAP / HANDOVER 与实现同步 |
| v1.0.2 | 播放逻辑对齐 Java | 曲目表滚动裁剪修复、随机播放按钮、洗牌袋、音量曲线、起播 |
| v1.0.3 | 单实例 + 应用内管理 | CreateMutexW 单实例 + 命名事件唤起；应用内设置 / 卸载（可保留数据） |
| v1.0.4 | 分享 / 下载 / 搜索 | 歌单分享码、下载确认弹窗、歌单内搜索（标题 + UP主） |
| v1.0.5 | 导入入口 | 「导入歌单」弹窗加「分享链接」，按钮统一中性灰 |
| v1.0.6 | **分享码 zlib 修复** | Java Deflater 产出 zlib 包装，C++ 原用 raw -> 解不开；改为双向兼容 |
| v1.0.7 | **导入封面** | view 没解析 pic -> 导入的歌没封面；补 VideoInfo.coverUrl + 「补全缺失封面」 |
| v1.0.8 | **播一半切歌** | httpGet 里 8 MiB 硬截断 -> 缓存只有半首；参数化 maxBytes（音频 256 MiB）+ 清旧缓存 |
| v1.0.9 | 加单曲 | 粘贴框「添加到歌单」，按 (bvid,cid) 去重 |
| v1.0.10 -> v1.1.0 | 输出设备（第一版）+ 编号进位 | 钉 mmdevice + 起播后重设 + 弹窗显示实际输出；版本号改逢十进一 |
| v1.1.1 | **输出设备真修复** | 进程内换设备无效 -> 按 --mmdevice-audio-device **重建 libvlc 实例**；启动先重建再起播；WASAPI 会话核对 |
| v1.1.3 | **撤销 + 扫描目标** | LibraryStore::Snapshot 整份快照 20 步 + Ctrl+Z；扫描本地音乐进「当前选中歌单」 |

注：v1.0.10 与 v1.1.0 是同一份二进制，仅编号进位，v1.0.10 的 release 与 tag 已删除。v1.1.2 的包打出后用户又提了撤销需求，合并进 v1.1.3 一起发。

---

## 6. 六个"看起来像玄学"的 bug 与真根因

### 6.1 手机端分享链接导入失败 —— zlib vs raw deflate

- Java 侧 new Deflater(Deflater.BEST_COMPRESSION)（1 参构造）= nowrap=false -> **zlib 包装**（RFC1950，78 9c / 78 da 头 + adler32 尾）。
- C++ 原用 raw deflate（-15）编解码 -> 手机端链接一律 DECODE_FAIL；C++ 生成的链接 Java Inflater() 也解不开。
- 修法：编码用 +15（zlib）；解码先 zlib、失败再 raw 兜底。
- 验证：tools/tmp/sharelink_test.cpp（编成 sharelink_test.exe）+ tools/tmp/mkjava_link_exact.py（Python zlib.compressobj(9, zlib.DEFLATED, 15)）。

### 6.2 导入的歌单没有视频封面

- BiliService::fetchVideo（x/web-interface/view）原来只解析 bvid/cid/title/owner/duration，**没有 data.pic**，VideoInfo 也没有封面字段。
- 分享码导入靠 fetchVideo 逐首补信息 -> 落库 coverUrl 全空 -> 列表/侧栏/封面弹窗都退化成占位（弹窗里还有 if (song->coverUrl.empty()) continue;）。
- 修法：VideoInfo.coverUrl + 解析 pic + 导入时写入 + setSongCover() + 弹窗「补全缺失封面」（26 首约 3 秒）。

### 6.3 歌播到一半自动切歌 —— httpGet 的 8 MiB 截断

- net/HttpClient.cpp 原有一行 if (response->body.size() > 8*1024*1024) break;，缓存下载与「下载歌曲」都走它。
- 超过 8 MiB 的歌只存前一半 -> 切本地播放后播到文件尾 Ended -> checkTrackEnd 判"播完" -> 自动下一首。
- 铁证：缓存目录里三个不同时长（7:08 / 5:51 / 4:52）的文件**大小完全相同 = 8,396,800 字节**（8 MiB + 一个 8 KiB 块）。
- 修法：httpGet(..., size_t maxBytes = 8MiB)，音频缓存/下载传 256 MiB；启动清一次旧截断缓存（标记 cache/.cache-v2-complete，本机清掉 29 个）；兜底——ended 但位置离总时长 >3s 判为损坏，删缓存改走流续播（同一首只重试一次）。

### 6.4 输出设备切换"只改了界面" —— VLC aout 只在创建时读设备

- VLC 3 的 mmdevice 输出在 aout 创建时读一次设备；**进程内一旦建过 aout，后续 libvlc_audio_output_device_set 或媒体项 :mmdevice-audio-device= 基本被忽略**。
- 用 tools/tmp/devicetest.cpp（直接驱动 VlcEngine）实测：起播前设设备（首次）生效；播放中设 + 重载媒体无效；媒体项再重载无效；**重建 libvlc 实例（--aout=mmdevice --mmdevice-audio-device=<id>）生效**。
- 还发现启动顺序 bug：engine_.init() 先跑、之后才读 audio.outputDevice，等于实例创建时没带设备。
- 修法：VlcEngine::switchAudioDevice(id)（释放 player/instance -> 带设备参数重建 -> 重新起播）；启动读完设置就重建实例。
- 校验：AppVolume::findSessionDeviceId() 枚举所有 render endpoint 找本进程会话（**跳过 Expired、优先 Active**），切换后 12 秒内每秒采样；弹窗显示「当前实际输出：xxx（已生效 / 未生效，正在重试）」。VLC 3.0 没有回读当前设备的 API（libvlc_audio_output_device_get 是 4.0 的），别白找。

### 6.5 扫描本地音乐全进"本地音乐"

- finishLocalScan 写死：先 findBySource("LOCAL","本地音乐")，再按标题「本地音乐」，都没有才新建；当前选中歌单根本没参与。
- 修法：扫描目标 = 当前选中歌单（复用 event.songId 存目标 id）；状态栏显示「新增 N 首到「歌单名」」。另加 BILIMUSIC_SCAN_DIR 环境变量跳过文件夹选择框（自动化用）。

### 6.6 UI 自动化点击总是点偏

- tools/tmp/shot3.ps1 用 GetWindowRect 取窗口矩形，点击点 = rect + (x,y)；**截图坐标与点击坐标在部分区域存在约 32px 垂直偏差**（实测点侧栏行要 +32 才准）。
- 教训：每次点击后**必须再截图确认状态变了**，点「删除 / 卸载」这类按钮前更要确认；曾误点打开卸载确认框（已取消，无损失）。

---

## 7. 坑位清单（红线）

1. **httpGet 默认 8 MiB**：任何下载二进制的地方都要显式传 maxBytes，否则静默截断。
2. **NSIS 脚本必须保留 UTF-8 BOM**：scripts/packaging/bilimusic.nsi 是 UTF-8 BOM；edit 工具会剥 BOM，改完用 python utf-8-sig 写回，否则中文安装界面乱码。
3. **VIProductVersion 必须 4 段**："版本号.0"，写死版本号会在进位后过期（现在用 CMake 变量拼接）。
4. **单实例 + 托盘**：CreateMutexW(L"Local/bilimusic-single-instance") + 事件 L"Local/bilimusic-show-window"（源码里是反斜杠分隔）；quitRequested_ 让主循环真正退出（卸载/退出必须用），否则进程残留锁住 exe，链接报 "cannot open output file bilimusic.exe"。
5. **卸载器会延迟自删目录**：不要"先跑 uninstall.exe 再马上装新版"，其延时 rd /s /q 会把刚装好的 payload 删掉（曾把 assets/ 整个删掉，界面全变 "?"）。卸载要等它跑完再装。
6. **ImGui 行要画在子窗口 drawlist 上**：曲目行若用父窗口 drawlist 会盖到表头（v1.0.2 修的就是这个）；点击也必须落在子窗口裁剪区内才生效。
7. **libvlc_media_new_path 要 Windows 反斜杠路径**：正斜杠 C:/... 在部分构建下返回 null（表现为"无法打开文件"）。
8. **miniz 3.0.2 拆分源文件要用 gcc 编**：miniz_tdef.c / miniz_tinfl.c 用 g++ 编报 s_tdefl_num_probes 重定义；另缺 miniz_export.h，项目里手写（MINIZ_EXPORT 定义成空）。
9. **source/ 分叉**：别 merge、别 force push，只读参考。
10. **真实用户数据**：当前 7 歌单 / 249 首（「星露谷」是用户新建的空歌单）。批量改动前 cp 一份 .bak-日期，改完用 python 校验每歌单曲目数。
11. **不要用 shell 内联拼复杂 PowerShell**：写 .ps1 文件再执行（踩过多次引号 / $ 被 bash 吃掉）。
12. **前台长任务（构建/推送）用后台 job**：run_in_background + job_output；前台 120s 会超时。

---

## 8. 数据与运行态

### 8.1 数据目录 C:/Users/amns2333/.bilimusic

| 文件/目录 | 说明 |
|---|---|
| playlists.json | 歌单 + 曲目（与 Java/Android 共用）。顶层 nextPlaylistId / nextSongId / playlists[] / songs[] / titleMigrationVersion；playlist 字段 id/title/createdAt/sourceType/sourceTitle/coverUrl/coverPath；song 字段 id/playlistId/cid/durationMs/sortOrder/bvid/title/artist/coverUrl/localPath |
| settings.json | 键：audio.full_cache、audio.network_cache、audio.outputDevice、audio_quality、cookie_blob、desktop.playmode、desktop.selectedPlaylist、desktop.volume、ui.sidebar_actions_expanded、ui.sidebar_collapsed、window.geometry（cookie_blob 是登录态，**别外传**） |
| cache/ | 整首缓存 bvid_quality.m4s + 标记 .cache-v2-complete |
| covers/ | 歌单封面本地图片 |
| cpp-app.log | C++ 版日志（中文，按行追加） |
| app.log | Java 版日志（历史） |
| playlists.json.bak-* | 历次备份（保留最近一份即可） |

sourceType 语义：FAV（收藏夹）/ TOVIEW（稍后再看）/ UPLOAD（UP主投稿）/ SEASON（合集）/ LOCAL（本地音乐桶）/ SHARE（分享码导入）/ MANUAL（手动新建）。

### 8.2 运行态

- 安装目录 D:/bilimusic（NSIS 按用户安装，375 个文件，exe 约 6.1 MB + vlc 约 135 MB）。
- 单实例；关窗口默认进托盘，托盘菜单可退出。
- 最近一次安装：v1.1.3，启动日志「bilimusic v1.1.3 启动」。

---

## 9. 调试与验证手册

### 9.1 日志关键词

    grep -a "播放结束，自动下一首" cpp-app.log | tail     # 带 pos/duration/ended，能区分正常播完与异常中断
    grep -a "整首缓存命中|在线播放：|切换到整首缓存播放"    # 播放链路
    grep -a "输出设备核对|设备观察|已按设备重建"           # 输出设备
    grep -a "本地音乐扫描|添加单曲到歌单|撤销上一步"        # 数据操作
    grep -a "缓存不完整|播放提前结束"                      # 缓存损坏兜底

### 9.2 环境变量开关

| 变量 | 作用 |
|---|---|
| BILIMUSIC_VLC_DIR | 指定 libvlc.dll 所在目录（自检时用） |
| BILIMUSIC_VLC_VERBOSE=1 | VLC --verbose=2，看 aout 模块与设备选择 |
| BILIMUSIC_SCAN_DIR | 扫描本地音乐时跳过文件夹选择框，直接用该目录（自动化测试用） |

### 9.3 独立测试程序（tools/tmp）

| 程序 | 用途 |
|---|---|
| sharelink_test.cpp/.exe | 分享码编解码（bm://pl/v1/...），支持 zlib 与 raw |
| mkjava_link_exact.py / mkdup_link.py | 用 Python 复刻 Java 端 zlib 编码，造测试链接 |
| devicetest.cpp/.exe | 直接驱动 VlcEngine 验证输出设备切换（打印 WASAPI 会话所在设备） |
| shot3.ps1 | 置顶 + 截图 + 点击（-clickX -clickY，注意 32px 偏差） |
| dbl2.ps1 | 双击指定坐标 |

独立程序编译示例：

    tools/w64devkit/bin/g++.exe -std=c++20 -O1 -Icpp/src -o tools/tmp/devicetest.exe \
      tools/tmp/devicetest.cpp cpp/src/media/VlcEngine.cpp cpp/src/media/AppVolume.cpp \
      cpp/src/util/AppLog.cpp -lole32 -loleaut32 -luuid -lshlwapi

### 9.4 数据核对片段（改完数据必跑）

    PYTHONIOENCODING=utf-8 python -c "
    import json,io
    d=json.load(io.open(r'C:/Users/amns2333/.bilimusic/playlists.json',encoding='utf-8'))
    for p in d['playlists']:
        print(p['id'], p['title'], p['sourceType'], sum(1 for s in d['songs'] if s['playlistId']==p['id']))
    print('total', len(d['songs']))"

### 9.5 UI 自动化注意

- 每次点击后截图确认（状态栏文字 / 头部标题 / 高亮行）。
- 危险按钮（卸载、删除歌单）先确认弹窗再点。
- 侧栏行 y 坐标容易差 30px：以截图文字中心为准，必要时 +32 试第二次。

---

## 10. 已知问题与建议下一步

已知小问题：

- 撤销栈**只在内存**，重启即清空；没有 redo（Ctrl+Shift+Z）。
- 撤销是整份快照，歌多时每步几十 KB，20 步上限；不影响正确性。
- 扫描本地音乐无增量提示；重复文件按 localPath 去重，换路径会重复。
- 「从歌单视频选封面」需要歌单里至少一首带封面的曲目；老数据要手动点一次「补全缺失封面」。
- 歌单列表与曲目表都没有手动拖拽排序。
- 沉浸式页（openImmersive）已实现但与播放状态耦合深，改动小心。

建议优先级（用户可能提）：

1. Ctrl+Shift+Z 重做 / 撤销栈持久化到 undo.json。
2. 歌单内拖拽排序（sortOrder 字段已就绪，缺 UI + LibraryStore::reorderSongs()）。
3. 批量添加单曲（一次粘贴多个 BV / 从一段文本提取所有 BV）。
4. 下载管理页（已下曲目、重下、打开目录）。
5. 播放缓存上限设置（当前无上限，cache/ 会一直涨）。
6. Windows SMTC / 媒体键（ISystemMediaTransportControls）。NetEase 审计结论：fade 包络、预加载、降级重试、SMTC 可参考，但**不要**移植 cloudmusic.dll（只有 16 个导出，播放器是静态链接的）。
7. 无缝衔接（gapless）——VLC 3 不支持，成本高。

---

## 11. 交接约定

1. 语言：代码注释、提交信息、Release notes 用中文；标识符英文。
2. 提交风格：release: vX.Y.Z —— 一句话 / fix(scope): ... / feat(scope): ...。
3. 发版必做：改版本 -> 打包 -> 装 D 盘验证 DisplayVersion 与启动日志 -> commit/tag/push -> gh release create（两个产物 + 校验和）。
4. 不动真实数据：批量操作前备份 playlists.json，用 python 校验前后条目数；不在用户数据上做试验性写入。
5. 不要 force push source/，不要试图合并其分叉历史。
6. 用户偏好：直接干、少解释；改完给实测证据（日志/截图）；发版后给 SHA256 并告知「已装到 D:/bilimusic」。

---

## 12. 十分钟上手路线

1. 读 cpp/README.md（功能清单与截图）。
2. 读 cpp/docs/ROADMAP.md（M1-M5 里程碑与版本历史）。
3. cd cpp && scripts/build.ps1 构建 -> 跑 build/bilimusic.exe，确认能登录、能播。
4. 看 src/app/App.h 的成员（AsyncEvent、UndoEntry、设备观察相关），再粗读 App.cpp 的 startOnlinePlayFor / checkTrackEnd / finishImport / applyOutputDevice / undoLastOperation —— 覆盖 80% 日常改动面。
5. 改网络/下载相关代码前先回看第 7 节坑 1（8 MiB）。
6. 发版照 3.2 抄。
