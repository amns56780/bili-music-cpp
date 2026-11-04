# bilimusic · C++ 桌面版（重构中）

> 这是 [bilimusic](https://github.com/amns56780/bili-music) 桌面端的 **C++ 重写版**。
> 原来的 Kotlin / Compose 版本继续维护、不受影响；这里是并行的新实现。

## 为什么另起一版

- 去掉对 **JRE/JDK 17+** 的依赖：Java 版安装包必须要求机器上装了 Java，C++ 版是原生 exe
- 更小的体积和内存占用、更快的启动（Java 版实测启动约 3.2s、Working Set 约 430MB）
- 顺带把播放内核（libVLC）的封装换成直接 C API，不再经过 VLCJ/JNA

## 当前进度（M1 / M1.5）

- [x] CMake + MinGW-w64 构建，依赖全部内嵌在 `third_party/`（ImGui / GLFW / nlohmann-json），不需要 Qt、vcpkg、Visual Studio
- [x] 深色界面骨架：左侧歌单栏 + 右侧曲目表 + 底部播放条
- [x] 读取 Java 版同一份数据文件 `%USERPROFILE%\\.bilimusic\\playlists.json`
- [x] libVLC 动态加载（优先 `程序目录\\vlc`，其次系统安装的 VLC），支持本地文件播放 / 暂停 / 停止 / 进度拖动 / 音量
- [x] M1.5：UI 与模块化组件对齐 Java 版（详见 `docs/ROADMAP.md`）

### M1.5 · UI 与模块化组件（已完成）

- [x] 自绘无边框标题栏：拖动窗口、双击最大化、最小化 / 最大化 / 还原 / 关闭（Win32 原生调用）
- [x] 侧栏模块：账号卡、粘贴链接入口、我的歌单（渐变封面占位 + 来源·曲目数）、底部「本地音乐 / 输出设备 / 导入歌单」
- [x] 歌单页模块：大封面、来源标签、标题/数量/创建日期、标签行、播放全部 / 多选 / 更多菜单、曲目表（播放行高亮 + 红色律动图标）
- [x] 播放条模块：当前曲目 / 播放模式 / 上一首 / 播放暂停 / 下一首 / 进度拖动 / 音量 / 输出设备 / 定时 / 队列
- [x] 可复用组件库 `src/ui/Components`：IconButton / PrimaryButton / GhostButton / SidebarAction / PlayCircleButton / ThinSlider / CoverPlaceholder / TagChip / 文本组件
- [x] 图标字体：Material Icons（Apache-2.0）合并进正文字体，按钮图标与 Java 版一一对应
- [x] 播放模式按钮循环切换（顺序 / 列表循环 / 单曲循环 / 随机），图标同步变化
- [x] emoji 兼容：Java 版数据里的 emoji 标题在 C++ 字体里没有字形，显示层统一替换成音符图标（数据不动）
- [x] M1.6：三个由 Java 版继承的痛点修复
  - **音量不再经过 VLC**：改用 Windows WASAPI 的「每应用音量」（ISimpleAudioVolume），拖动即时生效，libVLC 固定 100%；WASAPI 不可用时自动回退 libvlc
  - **窗口拖动改系统原生**：子类化 WndProc，标题栏 WM_NCHITTEST 返回 HTCAPTION → 系统 modal 拖动（DWM 直接搬窗口，UI 线程零参与）；四边/四角返回 HT* 获得原生缩放；WM_GETMINMAXINFO 最大化不盖任务栏。Java 版被 AWT 挡住做不到，C++ 版是原生方案
  - **封面/圆角抗锯齿**：圆角渐变从「逐行台阶」改为「轴对齐渐变条带 + 四角扇形 PathFillConvex（AA）」，圆形头像用抗锯齿实心圆
  - 修掉音量条每帧被重置回 80% 的 bug（PlayBarActions 默认值误参与比较）
- [x] **M2（核心链路已完成）**：WBI 签名 / Cookie 登录态 / 取流 / 在线播放
  - [x] Cookie：读写与 Java 版共用的 `~/.bilimusic/settings.json` 里的 `cookie_blob`，启动自动校验 nav 并显示头像/昵称/UID
  - [x] 登录弹窗（粘贴整段 Cookie）、退出登录；登录态失效提示
  - [x] WBI 签名：nav 取 img_url/sub_url → 重排表得 mixin_key（缓存 24h）→ 参数加 wts 排序 → md5 得 w_rid
  - [x] `x/web-interface/view` 取 cid/标题/UP主/时长
  - [x] `x/player/wbi/playurl` 取流（fnval=4048），失败强刷 WBI key 重试一次，再退到 `x/player/playurl`
  - [x] 音质选择：指定档位匹配 → 192K/132K/64K 降级链 → 最高带宽兜底；durl 兜底
  - [x] 在线播放：libVLC + `http-referrer`/`http-user-agent`（否则 bilivideo 403）
  - [x] 双击在线曲目播放、上一首/下一首自动取流、粘贴 BV 号/链接弹窗播放
  - [x] 音量写入节流（修「调完音量后一炸一炸」）：
  - 原因：每帧都写 WASAPI 音量（60 次/秒）+ 每帧写 libvlc 音量；音量写入自带短斜坡，高频重写会周期性爆音；
    会话重建瞬间还会回退去写 VLC 音量（cbrt(80)≈43），和 WASAPI 的 100% 来回跳
  - 修复：只在「音量变化 / 刚起播 / 会话重建」时写一次；平时 250ms 只读检查；WASAPI 模式永不回退写 VLC 音量
  - 新增会话过期检测：VLC 换曲/恢复重建音频输出后自动重新绑定并重新下发音量
- [x] 在线流暂停/恢复防断续：`:network-caching=3000` + `:http-reconnect=true`
  - 原因：VLC 默认网络缓存只有 1s，暂停时缓存被耗干，恢复瞬间要重连 mcdn 拉数据 → 断断续续
  - 自检工具：`bilimusic.exe --test-pause <本地音频>` / `--test-pause-online BV1xx`
    （用 WASAPI 峰值表每 50ms 采样，统计恢复后的静音采样比例；修复前 ~11%，修复后 ~0.8%，本地文件 0%）
- [x] 音质档位 UI：播放条进度条与音量组之间的小胶囊（自动 / 192K / 132K / 64K / Hi-Res / 杜比），
  切换后立即重新取流并从当前进度续播；档位写入 `settings.json` 的 `audio_quality`（AUTO/Q192/Q132/Q64/HIRES/DOLBY，与 Java 版共用）
- [x] 播放看门狗：在线曲目起播 6s 仍无进度 → 先切备用地址，再强制重新取流（覆盖 URL 过期场景）
- [x] 网络缓存拉到 20s（`:network-caching=20000`），暂停/恢复和弱网更稳
- [x] 音质胶囊按文字自适应宽度（之前固定 66px 会多出一截空白）
- [x] 音量支持鼠标滚轮微调（鼠标在音量组上滚动，每格 ±2%）
- [x] 字体整体再调大一档（正文 18 / 次要 15 / 标题 22 / 大标题 38），行高与表格同步放大
- [x] 字体改用「微软雅黑 Bold」（msyhbd.ttc），文字更粗更清晰
- [x] 音质胶囊文字居中、去掉右侧下拉三角（不再有空白）
- [x] 扫码登录：B 站 `qrcode/generate` + `qrcode/poll` 轮询（未扫码 / 已扫码待确认 / 过期 / 成功），
  成功后从 `Set-Cookie` 与回调 URL 解析登录态 Cookie 写入 settings.json，自动刷新昵称/UID；
  二维码用内置 qrcodegen（Nayuki, MIT，third_party/qrcodegen）编码、ImGui 直接画方块
- [x] M2 完成（登录 / WBI / 取流 / 在线播放 / 音质档位 / 备用地址 / 过期重取）
- [x] **M3（第一批）**：歌单管理与导入
  - [x] LibraryStore 支持写回 playlists.json（与 Java 版同格式、临时文件+改名安全落盘）
  - [x] 新建 / 重命名 / 删除歌单；重名对话框（覆盖 / 自动重命名加 "(n)" / 取消），规则与 Java 版 PlaylistNaming 一致
  - [x] 导入收藏夹：拉取"我创建的收藏夹"列表 → 选择 → 分页读取 → cid 补齐 → 去重写入；
        同一来源重复导入自动复用原歌单（不重复建）
  - [x] 导入稍后再看
  - [x] 导入进度显示（"正在导入… / 正在补齐音频信息 153/173"）
  - [x] 导入 UP 主投稿：输入 UID / 空间链接（WBI 签名 arc/search，分页 + cid 补齐）
  - [x] 导入合集：输入合集 ID / 链接（seasons_archives_list，合集接口无 cid，统一用 view 补齐）
  - [x] UP主/合集拿到真实昵称/合集名后才判重，重名同样走「覆盖 / 自动重命名 / 取消」
  - [x] 多选：勾选曲目、全选/清空、复制/移动到其它歌单（按 bvid+cid 去重）、批量删除
  - [x] 歌单封面：从歌单视频选封面（存 coverUrl）、从本地图片选封面（复制到 ~/.bilimusic/covers 并显示；
        引入 stb_image + GL 纹理，本地封面在歌单页大图与侧栏小图都能显示）
  - [x] 网络封面缓存：后台线程下载 B 站封面 → 主线程上传 GL 纹理（ImageCache），
        歌单大图/侧栏小图/播放条封面都显示真实封面（失败自动回退渐变占位）
  - [x] 下载当前曲目：当前在线播放的音频下载到 ~/.bilimusic/downloads/，并把 localPath 写回曲目
  - [x] 本地音乐扫描：选文件夹递归扫描（mp3/flac/wav/m4a/aac/ogg/opus/wma/ape），
        写入「本地音乐」歌单，本地曲目按 localPath 去重
  - M3 全部完成 ✅
- [x] 应用图标：与 Java 版同款 ic_launcher.png（glfwSetWindowIcon，多尺寸内嵌）
  和 exe 文件图标 bilimusic.ico（windres 资源）
- [x] 窗口位置/大小与音量记忆（settings.json 的 window.geometry / desktop.volume），
  下次启动沿用；关闭收进托盘时也会保存
- [x] 空闲未播放时底部播放条显示 BM 应用图标
- [x] **M4（核心项已完成）**
  - [x] 封面图：网络封面异步缓存（stb_image + GL 纹理）、本地图片封面、ContentScale.Crop 裁切
  - [x] 系统托盘：托盘图标 + 右键菜单（显示窗口 / 播放暂停 / 上一首 / 下一首 / 退出），
        点关闭按钮收进托盘、托盘退出才真正退出
  - [x] 全局媒体快捷键：媒体键 播放/暂停、上一首、下一首（全局键盘钩子，窗口隐藏也生效），
        另支持 Ctrl+Alt+Space/←/→ 与 Ctrl+Alt+H 显示窗口
  - [x] 窗口内快捷键：空格 播放/暂停、←/→（或 [ / ]）上一首/下一首
  - [x] 本地音乐扫描、下载当前曲目
  - [x] 沉浸模式（对齐 Java 版 Immersive.kt）：点击播放条封面**自下而上滑入**，
        默认背景是**桌面壁纸（高斯模糊）**，可切「自定义图片 / 纯色」；
        左上角时间+日期、右上角全屏/播放模式/音量/背景设置/关闭按钮，
        大封面 + 歌名 + UP主 + 可拖动进度条 + 「进入时间 · 已听时长」+ 上一首/播放/下一首，
        底部**真实频谱**律动线条（WASAPI loopback + 1024 点 FFT，可隐藏）、鼠标 3 秒空闲自动隐藏、
        滚轮 ±1% 音量；Esc 关闭，全屏按钮独立切换
  - [x] 播放队列面板：当前歌单列表、当前播放高亮、点击直接切歌
  - [x] 定时关闭：15/30/60/90/120 分钟，到期自动暂停，可取消
  - [x] 输出设备选择：枚举 WASAPI/mmdevice 设备并切换（写入 audio.outputDevice，重启恢复）
  - [x] 防熄屏：播放时 SetThreadExecutionState 阻止息屏/休眠
  - M4 核心项全部完成 ✅（歌词页 Java 版本身也没有做）
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
  app/App.{h,cpp}       应用状态 + 动作分发
  ui/Theme.{h,cpp}      设计 token（颜色/尺寸/字体，对应 Java 版 BmTheme.kt）
  ui/Components.{h,cpp} 模块化控件（按钮/滑块/封面占位/标签/文本）
  ui/Icons.h            Material Icons 码点 + UTF-8 编码
  ui/TitleBar.{h,cpp}   无边框自绘标题栏（拖动 / 窗口按钮）
  ui/Sidebar.{h,cpp}    侧栏
  ui/PlaylistPage.{h,cpp} 歌单页
  ui/PlayBar.{h,cpp}    底部播放条
  media/VlcEngine.{h,cpp}  libVLC 动态加载封装（播放/暂停/进度/音量）
  data/LibraryStore.{h,cpp} 读取 playlists.json（与 Java 版共用格式）
third_party/            imgui / glfw / nlohmann-json / icons（源码内嵌）
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
