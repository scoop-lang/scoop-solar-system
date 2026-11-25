# Scoop Solar System

使用 Scoop UI、CSPICE 与 Metal／GLES 3 编写的原生太阳系浏览程序。支持 macOS Apple Silicon（打包为 `.app`）和 Linux x86_64 GNU（GTK 4 Wayland 会话，打包为 deb），运行时不需要 Scoop 工具链或网络连接。

共 **31 个独立天体**：太阳、八大行星、冥王星，以及月球、火卫一／二、木星四大卫星、七颗土星卫星、五颗天王星卫星、海卫一和冥卫一。小行星带使用 2,200 个固定参数的代表粒子。

## 运行

### macOS

已生成的应用在：

```text
dist/aarch64-apple-darwin/prebuilt/dev.scoop.solar-system/app/Scoop Solar System.app
```

当前构建要求 Apple Silicon 与 **macOS 27.0.1+**（配套工具链生成的最低系统版本），在 macOS 27.2.1 实测。可双击打开，或在仓库根目录执行：

```sh
open "dist/aarch64-apple-darwin/prebuilt/dev.scoop.solar-system/app/Scoop Solar System.app"
```

完整应用约 631 MiB，主要体积来自离线星历。资源随 `.app` 保存；程序从应用包定位资源，可以复制到其他目录运行。

### Linux

`make bundle` 在 Linux 上生成：

```text
dist/x86_64-unknown-linux-gnu/prebuilt/dev.scoop.solar-system/deb/solar-system_0.1.0-1_amd64.deb
```

需要 GTK 4.18+、libadwaita 1.4+ 和提供 GLES 3 的 Mesa 等 EGL 驱动，在 Ubuntu 26.04 的 GNOME Wayland 会话实测。安装后运行 `solar-system`，资源位于 `/usr/share/dev.scoop.solar-system/solar/`。开发构建可直接在仓库根目录运行；指定 Wayland 显示时：

```sh
WAYLAND_DISPLAY=wayland-0 target/x86_64-unknown-linux-gnu/release/dev.scoop/solar-system/solar-system
```

首次打开显示当前 UTC，默认暂停，倍率为 1 天／秒。

## 操作

| 操作 | 效果 |
| --- | --- |
| 鼠标左键拖动 | 旋转视角 |
| 滚轮、触控板纵向滚动或捏合 | 缩放 |
| Shift + 左键拖动、中键拖动、横向滚动 | 平移观察中心 |
| 点击场景后按方向键 | 平移；按住 Shift 使用较小步长 |
| 双击天体 | 进入该天体近景 |
| 左侧天体目录 | 聚焦行星系统，展开其主要卫星；目录可以滚动和收起 |
| 全景／内行星／复位 | 回到相应观察范围 |
| 轨道／标签 | 切换参考轨道线和名称 |
| 天体悬浮卡片 → 查看详情 | 打开天体档案；详情临时暂停，关闭后恢复原来的播放状态 |
| Escape | 关闭详情或浮卡 |

底部浮动时间轴支持播放／暂停、正反向、六档倍率、日／月／年／世纪跨度，以及拖动定位。倍率从实时到每秒 10 年；拖动结束会恢复拖动前的播放状态。日期框按 UTC 接受 `YYYY-MM-DD` 或 `YYYY-MM-DDTHH:MM:SS`，按回车或“跳转”生效。“现在”返回当前 UTC。

可选时间为 **1900-01-01 至 2100-01-01 UTC**。到达边界会自动暂停，播放时可见时间窗口自动跟随。小窗口收起右侧资料，把详情入口移到工具栏。

## 从源码构建

macOS 需要 Apple Silicon、Xcode Command Line Tools；Linux 需要 x86_64 GNU、clang、pkg-config，以及 GTK 4、GdkPixbuf、EGL、GLESv2 开发文件（Ubuntu：`libgtk-4-dev`、`libgdk-pixbuf-2.0-dev`、`libegl-dev`、`libgles-dev`）。两者都需要 Python 3、curl 和配套 Scoop 工具链。已验证 macOS 使用 `0.0.0-dev.20261123`（revision `3910a8c5ee14722d0f15f46d3619b6a24bd6add4`），Linux 使用 `0.0.0-dev.20261124`（revision `f1bc9aa040bd7e69314592ca334313aa2c0f4298`）；不需要修改或重新构建 Scoop 编译器仓库。

在仓库根目录执行：

```sh
# 显式准备本机平台的 CSPICE、星历子集、37 张地图和原始许可说明。
make assets

# 如果 Scoop 已在 PATH 内：
make build
make run
make bundle

# 也可指定同级安装中的命令：
make bundle SCOOP=../scoop-install/bin/scoop
```

首次准备需要下载约 3.5 GiB 的官方原始数据，并生成约 611 MiB 的应用资源；建议留出 6 GiB 以上磁盘空间。下载脚本读取 `https_proxy` 等标准环境变量，支持断点续传。原始下载放在 `vendor/downloads/`，准备结果在 `assets/prepared/`，两者均不进入 Git。普通 build 不执行网络下载。

开发二进制默认从当前目录的 `assets/prepared/` 读取资源，也可通过 `SCOOP_SOLAR_RESOURCES` 指定资源目录。打包应用优先使用自身 `Contents/Resources/solar/`（macOS）或程序所在 `usr/lib/<identifier>/bin` 对应的 `usr/share/dev.scoop.solar-system/solar/`（Linux）。`make` 按主机自动选择 target 与打包配置。

## 验证

```sh
make format            # Scoop fmt、clang-format、固定版本 Ruff
make test              # scoop.test：时钟、星历、共同缩放与拾取
make test-native       # 连续星历覆盖、JPL Horizons 样本、真实 Metal／GLES 绘制
```

`make format` 需要 `clang-format` 与 `uv`。本机 Homebrew LLVM 可指定 `CLANG_FORMAT=/opt/homebrew/opt/llvm/bin/clang-format`。原始验证日志不提交，验证结果与平台限制记录在 [实施与验收](docs/IMPLEMENTATION.md)。

## 实现与边界

Scoop 负责窗口、时间、状态、相机、展示变换、CPU 拾取、时间轴和弹层。CSPICE 查询几何位置、速度及 IAU 姿态；Metal（macOS）或 GLES 3（Linux）通过现有 `GpuBuffer.withNativeWrite` 生成 GPU 图像，再由 Scoop Canvas 合成。普通场景绘制不读回 CPU 像素。

太阳是主光源，包含自发光、受光方向、昼夜分界、粗糙材质、地球云层／夜景／薄大气、天体间解析遮挡，以及土星环、天王星环及环影。天体大小单独放大，行星间距保持线性 AU；局部卫星系统采用共同放大系数。冥王星与卡戎均绕共同质心移动。光照与阴影使用真实位置、真实半径和有限大小的太阳，再映射到放大表面；展示比例及卫星是否显示不会制造或消除食现象。

地图表示代表性外观，不重建所选日期的真实天气。参考轨道是当前状态的两体近似线，位置仍来自 SPK。食现象使用物理几何近似，非球形遮挡物使用包围球，不用于精确接触时刻或地面路径预报；屏幕上的大小和间距仍是展示比例。图像来源与不同许可分别保留在 [第三方说明](THIRD-PARTY-NOTICES.md) 和随包 `solar/notices/`。

目前实际支持并验证的是 macOS Apple Silicon 与 Linux x86_64 GNU（Wayland）。Windows 后端、Intel macOS、Linux X11 会话与 aarch64 尚未验证；共享天文逻辑、相机与 UI 不依赖 Cocoa、EGL 或 GL 类型，渲染接口集中在 `native/include/solar_renderer.h`，原生句柄借用集中在 `platform/<os>/`。

- [设计与架构](docs/DESIGN.md)
- [星历、纹理与资料来源](docs/DATA-AND-ASSETS.md)
- [实施与验收](docs/IMPLEMENTATION.md)
