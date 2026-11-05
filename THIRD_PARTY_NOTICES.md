# 第三方许可说明 / Third-Party Notices

本项目代码以 **GNU General Public License v3.0（GPLv3）** 发布，完整文本见仓库根目录的 [`LICENSE`](LICENSE)。

发布包中包含以下第三方组件。各组件的版权归其作者所有，并按各自许可证分发。

## Qt 6.8.2

- 使用模块：Qt Core、Gui、Widgets、Concurrent、Network、Multimedia、Sql
- 版权：The Qt Company Ltd. 及 Qt 贡献者
- 许可证：Qt 6 开源版采用 **GNU LGPL v3 / GNU GPL v3** 双重许可（部分模块仅 GPLv3）
- 本项目按 **GPLv3** 使用 Qt，并保持**动态链接（DLL）**部署
- Qt 源码与许可说明：<https://www.qt.io/download-open-source>
- LGPLv3 全文：[`LICENSES/LGPL-3.0.txt`](LICENSES/LGPL-3.0.txt)；GPLv3 全文：[`LICENSE`](LICENSE)

## FFmpeg

- 用途：Qt Multimedia 的 FFmpeg 后端，负责音频/视频解码
- 版本：FFmpeg 7.1（LGPL 2.1 or later）
- 发布包中的 `avcodec-61.dll`、`avformat-61.dll`、`avutil-59.dll`、`swresample-5.dll`、`swscale-8.dll` 为动态链接库
- 源码：<https://ffmpeg.org/download.html>
- LGPL 2.1 全文：[`LICENSES/LGPL-2.1.txt`](LICENSES/LGPL-2.1.txt)

## MinGW-w64 运行时

- 发布包中的 `libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll` 来自 MinGW-w64 GCC 13.1
- `libgcc` / `libstdc++` 按 GPL with GCC Runtime Library Exception 分发；`libwinpthread` 按其 BSD 风格许可证分发
- 说明：<https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html>

## Microsoft DirectX 运行时

- 发布包中的 `D3Dcompiler_47.dll` 为 Microsoft DirectX 运行时组件
- 按 Microsoft 的再分发许可随 Qt 应用一起分发

## 游戏素材

- 《异环》游戏截图、地图瓦片、场景/角色素材的版权归 **Hotta Studio / 完美世界** 所有
- 素材**不适用**本项目的 GPLv3 许可，不得声称由本项目授权
- 本项目为非商业同人作品，素材仅用于非商业的造型、配色和场景比对；如权利人提出要求，将移除相关内容

---

如果发布二进制包，请将本文件、`LICENSE` 和 `LICENSES/` 目录一起放入发布包。
