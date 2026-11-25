# 实施与验收

日期：2026-11-24。macOS Apple Silicon 首版已完成应用实现、星历和纹理准备、原生构建与 `.app` 打包。2026-11-25 增加 Linux x86_64 GNU（GTK 4 / Wayland、GLES 3）后端、资源定位与 deb 打包，验证记录见第 8 节；同日增加 Windows x86_64（MSVC UCRT、D3D12）后端、资源定位与 NSIS 打包，验证记录见第 9 节。以下记录实际实现和验证，不将其他平台接口的存在视为已经支持其他系统。

## 1. 实际目录与职责

| 位置 | 职责 |
| --- | --- |
| `src/app/` | 应用入口、窗口、共享模型、天体聚焦、状态与轨道缓存 |
| `src/astronomy/` | 31 个天体的资料、类型化身份、CSPICE 适配、模拟时钟 |
| `src/scene/` | Double 向量、相机、物理／展示变换、帧快照、投影与拾取 |
| `src/render/` | GPU 设备、输出缓存、资源所有权与 C ABI 后端调用 |
| `platform/darwin/`、`platform/linux/`、`platform/windows/` | 各平台唯一的原生 GPU 句柄借用：目标格式、Metal／EGL／D3D12 设备与纹理、关闭时的 context 要求 |
| `src/ui/` | 场景视口、目录、时间轴、悬浮卡片、详情、窄窗口布局 |
| `native/astronomy/` | CSPICE 错误处理、状态／姿态、参考轨道和时间转换 |
| `native/macos/` | Objective-C / C、资源定位、Metal 管线与场景编码 |
| `native/linux/` | C、资源定位、GdkPixbuf 纹理解码、GLES 3 程序与场景编码 |
| `native/windows/` | C++、资源定位、WIC 纹理解码与 CPU mip、D3D12 根签名／管线、帧上传环与场景编码 |
| `native/shaders/solar.metal`、`solar.glsl`、`solar.hlsl` | 表面、环、轨道、星空、小行星带与太阳辉光；GLSL ES 3.00 与 HLSL 5.1 为 Metal 版本的逐项翻译 |
| `scripts/` | 显式准备内核、地图与原许可；嵌入 shader；参考数据比对 |
| `tests/` | Scoop 行为与数值测试 |
| `tests-native/` | 星历连续覆盖、独立参考样本与真实 Metal／GLES／D3D12 测试；GLES 与 D3D12 共用 `target_shadows.c` |
| `packaging/macos.toml`、`packaging/linux.toml`、`packaging/windows.toml` | 离线 `.app`、deb 与 NSIS 配置 |

没有修改同级 Scoop 编译器、runtime 或 UI 库。共享逻辑与渲染器之间使用实际调用的 `solar_renderer_*` C 接口，不引入通用游戏引擎、后端注册框架或空的其他平台实现。Metal、GLES 与 D3D12 后端各自持有天体／轨道的同形状态记录，没有为两份短小的状态函数另建共享 C 层。

## 2. 已实现的用户功能

- 全景、内行星、行星系统和卫星近景；旋转、缩放、平移、复位与跟随。
- 太阳、八大行星、冥王星和 21 颗主要卫星，以及 2,200 个代表性小行星粒子。
- 1900—2100 UTC 离线时间范围，锚点时钟、播放／暂停、反向、六档倍率、日期输入、拖动与时间窗口。
- 行星位置、速度、自转姿态由同一 ET 时刻求值；不同顺序跳转得到同一状态。卫星绕所属系统运行，冥王星／卡戎共同质心保持完整。
- 天体尺寸与局部卫星间距分别按展示比例放大，行星间距保持线性 AU。
- 每个天体独立纹理、太阳主光、粗糙材质、地球云层／夜景／大气边缘、天体遮挡、土星／天王星环及环影。光照使用真实物理几何及有限日面，映射回放大表面；隐藏卫星仍保留物理遮挡。
- Accepted 帧上的 CPU 最近交点拾取、悬浮基本资料、带图像的详情、临时暂停、Escape 和焦点恢复。
- 暂停重用 GPU 图像，窗口隐藏时停止主动帧需求；恢复时从单调时钟锚点计算时间。
- 中文界面、可滚动目录、窄窗口布局、日期错误和边界提示、离线资源与完整来源说明。

## 3. 自动验证

使用 Scoop `0.0.0-dev.20261123`，revision `3910a8c5ee14722d0f15f46d3619b6a24bd6add4`，配套 sysroot。格式化／检查先于验证：Scoop fmt、clang-format、Ruff 0.16.10，原生代码以 `-Wall -Wextra -Werror` 编译。

| 验证 | 结果 |
| --- | --- |
| `make test` | 3 个测试容器、10 项测试全部通过 |
| 时钟 | 倍率切换连续性、seek、拖动取消／恢复、正反向、边界、长时间无帧间隔 |
| 星历与场景 | 全部 31 个天体可聚焦并生成轨道；最大／最小日期及回到原时刻无路径依赖 |
| 展示与拾取 | 冥王星／卡戎共同缩放、最近表面遮挡、椭球投影与拾取一致、身份唯一 |
| 相机与时间组合 | 平移后的投影偏移正确，跟随地球跳转一天及一个月后仍保留观察偏移 |
| SPK 连续覆盖 | 40 个天体／参考中心目标在完整 UTC 区间内无缺口 |
| 逐年状态与姿态 | 31 个天体 × 201 个年首，共 6,231 个样本通过；旋转轴长度、有限状态与参考轨道起点通过 |
| 时间与错误处理 | 2016 年闰秒、无效日期错误清理后再次调用正常 |
| 真实 GPU | Apple M3 Ultra 上五条 Metal 管线、37 张地图、31 个独立天体帧通过 |
| 颜色与确定性 | 灰度卫星 RGB 一致；地球昼面中心亮度 95.8、夜面 2.4；相同输入帧逐字节一致 |
| 假日食回归 | 2027-01-07 10:05:08 UTC，加入月球物理遮挡前后，整幅地球测试图像逐字节一致 |
| 真实遮挡 | 2027-02-06 环食、2027-08-02 全食、2026-03-03 月食、木卫一投影到木星、木星遮住木卫一，真实 Metal 像素差分均检出阴影；环食保留余光 |
| 展示比例独立性 | 将月球展示半径放大 100 倍并移动展示位置，阴影帧逐字节一致；地球／土星及相机距离共同放大 4 倍后，每通道平均像素差小于 0.2；隐藏的遮挡物仍参与计算 |

独立参考样本来自 JPL Horizons API，时刻 JD 2451545.0 TDB、中心太阳、ECLIPJ2000 / ICRF、几何状态 NONE。版本化小样本与来源存于 `tests-native/horizons-j2000.json`，原始 API 日志放在忽略目录。

| 目标 | 位置差 |
| --- | ---: |
| 月球 | 0.002927 km |
| 地球 | 0.003156 km |
| 木星 | 3.395876 km |
| 卡戎 | 0.860165 km |
| 冥王星 | 0.860164 km |

最大的速度差约 0.000000252 km/s。木星、冥王星系统的差异包括 Horizons 与随包基础星历组合的差异；没有把这些样本宣称为全部米级精度。比对容限为 5 km 与 0.00001 km/s。

针对用户报告的 2027-01-07 10:05:08 UTC，另行从 JPL Horizons 查询同定义的地球／月球状态，保存于 `tests-native/horizons-2027-01-07.json`（含完整查询参数）。应用位置与独立参考分别相差 0.018250 km、0.015785 km，该组容限为 0.05 km。地心日月中心夹角约 5.666°、月球地心黄纬约 −3.240°，月球阴影轴距地心约 40,256 km；地球半径加该处半影半径约 10,034 km，没有日食。原实现以展示半径和展示位置做点光源遮挡，制造了截图中的巨大假影，已改为物理几何计算。

正例日期与 [NASA 日食表](https://eclipse.gsfc.nasa.gov/SEdecade/SEdecade2021.html)、[NASA 月食表](https://eclipse.gsfc.nasa.gov/LEdecade/LEdecade2021.html)对应；测试使用食过程中的 UTC 样本，不把当前渲染近似作为接触时刻预报。木星系统样本来自同一组 JUP365 星历：2027-01-07 11:00 UTC 的木卫一影，以及 2027-01-08 08:10 UTC 的木卫一受食。

## 4. 实际窗口验证

环境：Mac Studio，Apple M3 Ultra，macOS 27.2.1。默认内容区 1280 × 800 逻辑像素，Retina 输出；也验证了缩窄窗口后的布局变化。通过原生应用界面实际操作：

- 地球／月球系统切换，修正 LA 云图透明度后检查海陆、云层和夜侧。
- 更新物理光照后，重新打包并在实际窗口恢复到 2027-01-07 10:05:08 UTC：地球表面的巨大假影消失，地月展示尺寸仍保持放大；窗口保留在该时刻、地球系统、暂停／反向／1 天每秒状态。
- 土星近观与鼠标旋转，检查环带、卡西尼缝、前后遮挡及环影。
- 木卫一近景、灰度卫星地图、悬浮卡片、卡片详情、Escape 与稳定焦点入口。
- 输入 J2000 日期，拒绝 2200 年越界日期，运行到 2100 年自动暂停，再从边界反向运行。
- 运行中拖动时间轴后恢复原播放状态，改变倍率与时间窗口；窗口缩窄时详情入口移到顶部。
- 播放时打开详情保持冻结日期，关闭后恢复播放；原本暂停时关闭后继续暂停。
- 详情标题、独立尺寸的关闭按钮和内容排版经截图复验；按钮关闭、Escape 与焦点恢复正常。
- 点击场景后方向键平移，画面位移与预期一致；纵向滚轮缩放正常。

本次自动化未完成触控板捏合、横向滚动与 Shift／中键组合拖动的实机验收；相应输入分支已实现，平移通过方向键和数值测试验证。横向滚动自动化没有产生可见位移，尚不能据此确认真实触控板输入效果。

静止场景采样：10 秒墙钟时间内进程 CPU 时间增量为 0.00 秒（`ps` 计时精度）；测试过程中驻留内存约 0.8—1.0 GiB，随已加载纹理与预览数量变化。该数值是本机观测，不是其他硬件的性能保证。没有进行覆盖全部场景的帧时间基准，不宣称所有设备都达到 60 FPS。

## 5. 打包和资源

`make bundle` 先构建 release 可执行文件，再使用独立打包配置生成应用。完整应用约 630.34 MiB；其中离线资源约 610.81 MiB。按文件字节计量，文件系统分配空间可能更大。

已验证打包可执行文件从 `/tmp` 工作目录启动，资源仍来自应用包；也已通过系统应用启动并读取到实际界面。可执行文件仅动态依赖 macOS 系统库和框架，不依赖开发仓库中的动态库；`codesign --verify --deep --strict` 通过本地签名验证。

系统启动复验发现 CSPICE N0067 的 f2c 文件层先以读写模式打开内核，进程在该打开操作等待。应用适配改为只读模式后，在同一应用包位置重新启动并完成窗口交互；实际七个 SPK 文件描述符均为只读。该修改只作用于应用的静态链接库，未更改系统权限或官方准备工具。

没有改变用户的网络设置。应用运行路径不执行 HTTP 查询或素材下载；所有星历、地图、时间表和资料随包提供，网络只用于显式准备脚本。完整断网操作没有单独改变系统网络后实测。

当前可执行文件与 Info.plist 的最低 macOS 版本为 **27.0.1**，实际运行验证版本为 27.2.1。只验证了 aarch64；Intel、Rosetta 和更旧 macOS 版本未验证。

## 6. 移植边界

| 模块 | 其他系统所需工作 |
| --- | --- |
| 资料、时间、相机、场景、拾取、UI | 复用 Scoop 代码，验证目标平台窗口和输入 |
| CSPICE | 使用对应官方 C 工具包，复用同一套 SPK/PCK/LSK |
| 渲染 | 实现 `solar_renderer.h`；Linux 已使用 GLES 3，Windows 已使用 D3D12 |
| GPU 资源 | 增加 `platform/<os>/backend.scoop`，提供目标格式、设备／纹理借用和关闭方式 |
| 资源定位和打包 | 增加对应 `resources` 原生文件与实际平台 bundle 配置 |

没有为未实现平台增加占位类。纹理、物理数据、C 数据记录及材质计算边界可复用；其他后端仍需真实实现与图像对照验证。

## 7. 展示精度的限制

天体大小、卫星间距、亮度均服务展示；光照和阴影始终使用真实几何，不随展示比例变化。阴影采用平均半径、非球形天体的包围球与视圆盘重叠近似，多个遮挡物的透光比例相乘，环影仍按太阳中心方向采样；不包含大气折射造成的月全食红色，也不提供精确接触时刻或地面路径预报。

地图是代表外观，部分未知地表沿用上游标明的低细节补全，云层不随历史天气变化。参考轨道使用当前两体轨道要素，实际位置使用 SPK。未来 UTC 按随包 `naif0012.tls` 转换，不预测未来新增闰秒。

## 8. Linux 验证（2026-11-25）

环境：Ubuntu 26.04.1 LTS，x86_64，GNOME Wayland 会话 `WAYLAND_DISPLAY=wayland-0`；Intel Arc A770M（DG2）与 Iris Xe，Mesa GLES 3。Scoop `0.0.0-dev.20261124`，revision `f1bc9aa040bd7e69314592ca334313aa2c0f4298`，配套 GNU sysroot。CSPICE 使用官方 N0067 `PC_Linux_GCC_64bit` 包；星历子集与 37 张地图由同一准备脚本在本机重新生成。格式化／lint：Scoop fmt、clang-format、Ruff 0.16.10 通过，原生代码以 `-Wall -Wextra -Werror` 编译。

| 验证 | 结果 |
| --- | --- |
| `make test` | 3 个测试容器、10 项测试全部通过 |
| SPK 连续覆盖与逐年样本 | 40 个目标无缺口；6,231 个状态与姿态样本通过 |
| JPL Horizons 比对 | 两组样本的位置／速度差与第 3 节 macOS 记录逐位一致 |
| 真实 GLES（`make test-native`） | 五个程序、37 张地图、31 个独立天体帧通过；灰度卫星 RGB 一致；地球昼面中心亮度 94.6、夜面 2.3；相同输入帧逐字节一致 |
| 物理阴影 | 2027-01-07 假日食整幅一致；2027-02-06 环食（最暗比 0.318）、2027-08-02 全食（0.016）、2026-03-03 月食、木卫一影与木卫一受食均检出；月球展示放大 100 倍逐字节一致，地球／土星共同放大 4 倍差异小于 0.2 |
| 深度路径 | 本机提供 `GL_EXT_clip_control`；另以 `MESA_EXTENSION_OVERRIDE=-GL_EXT_clip_control` 强制投影重映射路径，上述全部断言同样通过 |
| deb 打包 | `make bundle` 生成 `solar-system_0.1.0-1_amd64.deb`（约 562 MiB）；解包到临时目录、从 `/` 启动，7 个 SPK 均从 `usr/share/dev.scoop.solar-system/solar/kernels/` 以 `O_RDONLY` 打开 |

GLES 原生测试使用 EGL surfaceless context 与自建 RGBA8 纹理，按 Skia 左上原点读回，断言与 Metal 测试相同。Linux 的 `make test-native` 依赖系统 `clang`、`pkg-config` 以及 GdkPixbuf／EGL／GLESv2 开发文件。

实际窗口：release 程序和解包后的 deb 程序均在 `wayland-0` 上启动并持续运行，未报告初始化、星历、纹理或 GPU 错误；Wayland 协议日志确认窗口收到 activated configure，并以 200% 缩放持续提交 2426 × 1284 像素的缓冲。用户在该显示上实际操作 release 程序：拖动旋转、滚轮缩放、目录中聚焦土星与地球、悬浮卡片及详情、时间轴播放，均确认正常。

限制与观察：

- GNOME Shell 拒绝无交互截图，本记录没有截图；窗口画面以用户的人工核对为准，没有逐项像素对照 macOS 画面。
- deb 未安装到系统；`Depends` 按本机实际 SONAME 对应的 Ubuntu 包填写（GTK 4、libadwaita、GdkPixbuf、EGL、GLESv2、fontconfig、freetype）。配套工具链链接的程序直接 `NEEDED` 约 140 个库（含 GStreamer、cups、curl 等 GTK sysroot 的传递依赖），在只装最小 GTK 运行时的系统上可能需要更多包；这属于 Scoop 链接输出，未在应用中修改。
- Linux 原生 include 使用 Debian／Ubuntu 的 GdkPixbuf／GLib 头文件路径（`/usr/include/gdk-pixbuf-2.0` 等）；其他发行版需按实际路径调整。
- 在 `wayland-0` 内嵌套 weston 14 测试时，kiosk-shell 下本应用的 GTK 客户端在首帧提交后被 weston 以 `error in client communication` 断开，而同一 weston 中 Scoop 自带的 `ui-shadcn`、`gtk-gpu` 示例正常；desktop-shell 下应用正常运行但 `weston-screenshooter` 不返回。尚未定位到 Scoop 或应用的具体问题，GNOME 会话不受影响。
- 未验证 X11 会话、NVIDIA／AMD 驱动、Linux aarch64、rpm／AppImage，以及触控板手势。
- 本次同时调整了 macOS 使用的共享 Scoop 代码：Metal 句柄借用从 `renderer.scoop` 移到 `platform/darwin/backend.scoop`，纹理加载改在 `GpuDevice.withNative` 借用内执行。Metal 原生代码、着色器和 `shaders.m` 未改动，但本机工具链没有 `aarch64-apple-darwin` sysroot，这部分改动尚未在 macOS 上编译或运行，第 3—5 节的 macOS 结果仍对应此前的提交。

## 9. Windows 验证（2026-11-25）

环境：Windows 11 Pro 10.0.26300，x86_64，NVIDIA GeForce RTX 4090；Visual Studio 18 Build Tools（MSVC 14.51.36231）与 Windows SDK 10.0.26100.0。Scoop `0.0.0-dev.20261124`，revision `f1bc9aa040bd7e69314592ca334313aa2c0f4298`（驱动 `sco.exe`，目标 `x86_64-pc-windows-msvc-ucrt`）。星历子集与 37 张地图由同一准备脚本在本机重新生成，资源总计约 611 MiB。格式化／lint：Scoop fmt、clang-format 23.1.5（对已有文件无改动）、Ruff 0.16.10 通过；应用原生代码以 `-Wmost -Wextra -Werror` 编译。

| 验证 | 结果 |
| --- | --- |
| `make test` | 3 个测试容器、10 项测试全部通过 |
| SPK 连续覆盖与逐年样本 | 40 个目标无缺口；6,231 个状态与姿态样本通过 |
| JPL Horizons 比对 | 两组样本的位置／速度差与第 3 节 macOS 记录逐位一致 |
| 真实 D3D12（`make test-native`） | 五条管线、37 张地图、31 个独立天体帧通过；灰度卫星 RGB 一致；地球昼面中心亮度 95.8、夜面 2.4；相同输入帧逐字节一致 |
| 物理阴影 | 2027-01-07 假日食整幅一致；2027-02-06 环食（最暗比 0.315）、2027-08-02 全食（0.016）、2026-03-03 月食、木卫一影与木卫一受食均检出；隐藏遮挡物、展示比例独立性与土星环断言通过 |
| NSIS 打包 | `make bundle` 生成 `solar-system-0.1.0-x86_64.exe`（约 507 MiB）；静默安装到临时目录：程序、`MSVCP140`／`VCRUNTIME140`／`VCRUNTIME140_1` 与 `resources/solar/` 布局正确；从 `C:\Windows\Temp` 启动，VC 运行库从安装目录加载、资源从安装目录读取；静默卸载后安装目录、卸载登记与开始菜单项均移除 |

实现中的平台问题与处理：

- NAIF 官方 `PC_Windows_VisualC_64bit` 的 `cspice.lib` 全部 2,229 个目标文件要求静态 CRT `LIBCMT`，与 Scoop MSVC 目标的动态 UCRT 冲突（链接报 LNK4098）。准备脚本改用同一 N0067 包内源码、`mkprodct.bat` 的同组宏，以工具链 `clang-cl /MD` 重新编译 `cspice.lib`；f2c 生成代码依赖隐式 `int`，仅为这批源码关闭 `-Wimplicit-int` 错误。`spkmerge.exe` 等工具使用官方预编译版。
- clang-cl 把 `-Wall` 解释为 `-Weverything`，而 `c_flags` 不能按目标区分，因此 Cone 的 C／C++ 标志改为 Scoop graphics Cone 同样使用的 `-Wmost -Wextra -Werror`；macOS／Linux 的 `make test-native` 仍以 `-Wall -Wextra -Werror` 编译同一批源码。
- `windows.h` 把 `near`／`far` 定义为宏，HLSL 中 `point`、`linear` 为关键字，对应标识符已改名。D3D12 没有点精灵，粒子与辉光改为顶点着色器展开的四边形。
- WIC JPEG 解码是加载地图的主要耗时：本机 37 张 4096×2048 级地图共约 10 秒（单张 0.05—0.6 秒），CPU mip 生成每张约 15—50 毫秒。应用只在天体首次可见时加载其地图，没有并行解码。

实际窗口：开发构建与安装版均在本机桌面启动（逻辑 2000×1292、物理 2586×1671 像素的窗口），通过 SendInput 自动操作并截图核对：全景场景、太阳辉光、轨道与标签；拖动旋转为侧视；悬浮在太阳上出现基本资料卡；目录聚焦土星后展开卫星列表；播放推进时间、再次点击暂停；土星近观可见环、卡西尼缝、卫星与环影；详情弹层显示实时渲染的土星图像与资料，关闭按钮正常。暂停的静止场景 10 秒内进程 CPU 时间增量 0.06 秒，工作集约 265 MiB；关闭窗口后进程以 0 退出且无错误输出。

限制与观察：

- 只验证了 NVIDIA RTX 4090 一种 D3D12 设备；AMD、Intel、WARP 软件设备，以及 Windows 10、ARM64、高对比度主题未验证。原生测试使用 DXGI 默认适配器，应用使用 Scoop 选定的设备。
- 滚轮缩放、方向键平移、双击聚焦、日期输入与 Escape 未在本次 Windows 自动化中单独确认；触控板与触摸输入未验证。
- 安装器只验证了按用户安装、静默安装与卸载；未签名，未测试升级、按机器安装和 MSI。MSVC C++ 运行库按 `vc-runtime = "app-local"` 随程序部署，取自构建机 Build Tools 的 Redist 目录。
- 资源路径含非 ASCII 字符时改用系统 8.3 短路径供 CSPICE 读取；该分支未在实际非 ASCII 安装目录验证，卷若禁用 8.3 名称则 CSPICE 可能无法打开内核。
- 本次的共享改动：`solar_renderer.h`／`solar_ephemeris.h` 增加 C++ 链接保护与 D3D12 渲染入口；GLES 测试的目标函数改名为通用 `render_target.h`／`target_shadows.c`；Cone 的 native 告警标志如上调整。这些改动未在 macOS 与 Linux 上重新编译或运行，第 3—8 节的结果仍对应此前的提交。
