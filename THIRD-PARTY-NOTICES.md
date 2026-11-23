# 第三方数据与素材

应用的位置、姿态与时间转换使用 NASA/JPL NAIF 的 CSPICE N0067、SPK、PCK 与 LSK。CSPICE 由 Caltech 在 NASA 合同下开发；原始免责声明保存在随包的 `notices/CSPICE-SpiceUsr.h`，版本说明在 `CSPICE-version.txt`。

七份 `solar-*.bsp` 是使用官方 SPKMERGE 提取目标与时间区间的子集。每份文件保留原始模型说明与贡献者注释，追加本项目制作说明。来源及目标列表见 `docs/DATA-AND-ASSETS.md` 和 `scripts/prepare.py`。

天体地图来自 [CelestiaContent](https://github.com/CelestiaProject/CelestiaContent)，固定到提交 `1bbbf41ed4639a17fd9a884fd843e425caedd9a3`。逐项文件、原始路径及许可说明入口记录在 `assets/sources/textures.json`；该清单也随包保存为 `notices/textures.json`。图像贡献者包括 NASA、JPL、USGS、任务观测团队，以及 Celestia 社区注明的地图处理作者。

应用保留了上游 README、LICENSE.md、各图像单独的 `.license` 文件，以及它们引用的许可文本，位于 `notices/CelestiaContent/`。没有单独 `.license` 的图像按上游 README 的逐项来源说明处理。各项许可不相同；上游程序的 GPL 声明不替代每张地图的使用条件。项目没有用 AI 生成未知地表来冒充观测数据；上游有补全或重建的部分在天体外观说明中标出。

完整应用资源位于 `.app/Contents/Resources/solar/`，不需要运行时连接这些网站。Scoop UI 与图形栈由配套 Scoop 工具链链接，其第三方依赖按该工具链附带的许可提供。
