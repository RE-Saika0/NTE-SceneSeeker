# NTE SceneSeeker

异环图寻小游戏，暂时只有看游戏内截图选地点的模式（本来是想做全景环绕的，但是没有方便的截图方式，就先搁置了）。
支持单人和 2–4 人局域网对战。
交流群：1131251806

## 玩法

看一张《异环》游戏内截图，在地图上标出它是在哪里拍的，越准分越高。

- 单人：默认 5 回合，可在设置里调整回合数和每回合时间。
- 多人：2–4 人局域网房间，每人 5000 血，每回合按猜测点与正确答案的距离扣血，最后存活者获胜。
- 题库由 `questions.nteq`（SQLite 元数据）和 `media.ntep`（图片包）组成，只有 `approved` 的 photo 题会进入出题池。

## 仓库内容

```text
qt/                      游戏客户端源码（Qt 6 Widgets / C++17）
LICENSE                  GPL-3.0 全文
THIRD_PARTY_NOTICES.md   Qt、FFmpeg、MinGW 等第三方许可说明
LICENSES/                LGPL-3.0、LGPL-2.1 许可证全文
```

本目录是游戏客户端工程；`assets/`、`build/`、`dist/` 等运行素材和构建产物不进入 git。`docs/`、`tools/` 是本地开发资料，也不进入公开仓库，运行所需素材见下节。

## 构建

需要 Windows 10/11 x64、Qt 6.8.2 MinGW 64 位、MinGW 13.1、CMake 3.21+。

```powershell
cmake -S qt -B build\qt -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH=<Qt 路径> `
  -DCMAKE_C_COMPILER=<MinGW 路径>/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=<MinGW 路径>/bin/g++.exe

cmake --build build\qt --parallel 8
```

产物：`build\qt\NTE-SceneSeeker.exe`

构建后 CMake 会自动执行 `windeployqt`，把 Qt DLL、平台插件和 MinGW 运行时放到 `build\qt`，可以直接双击运行。如果要复制到其他机器运行，请按下面的“发布”步骤用 `windeployqt` 部署到发布目录。

## 运行所需素材

把 `assets/` 放到 exe 同目录或项目根目录：

```text
assets/
├─ audio/bgm/                 背景音乐（MP3）
├─ audio/sfx/                 音效（WAV）
├─ map/tiles/                 地图瓦片金字塔
├─ map/map-config.json        地图尺寸/瓦片配置
└─ data/
   ├─ questions.nteq          题库元数据
   ├─ media.ntep              题库图片包
   ├─ map_labels.json         地图标签
   ├─ areas.json              地区/地点数据（工具和 Workbench 使用）
   └─ locations.json          地点记录（工具使用）
```

素材不随仓库提交。缺少题库时游戏会使用占位题目；缺少音频或地图时对应功能为空，但仍可启动。

## 发布（绿色包）

本仓库不附带打包脚本，发布时按下面的步骤手动组装：

1. 按“构建”一节生成 `build\qt\NTE-SceneSeeker.exe`。
2. 用 `windeployqt` 把 Qt DLL、平台插件和运行时复制到发布目录：

   ```powershell
   windeployqt --release --compiler-runtime --no-translations --no-opengl-sw `
     --dir dist\NTE-SceneSeeker build\qt\NTE-SceneSeeker.exe
   ```

3. 把 `LICENSE`、`THIRD_PARTY_NOTICES.md`、`LICENSES/` 复制到程序目录，程序包不包含 `assets/`。
4. 将程序目录压缩为 `NTE-SceneSeeker-<版本>-win64.zip`。
5. 将 `assets/` 单独压缩为 `NTE-SceneSeeker-<版本>-assets.zip`，压缩包顶层必须是 `assets/` 文件夹。

用户需要下载两个包，解压到同一个目录后运行 exe。

Qt 请保持 **DLL 动态链接**，不要把 Qt 静态链接进 exe。Qt 6 开源版采用 LGPLv3/GPLv3 双重许可，动态链接最便于满足许可证要求，也方便单独替换或升级 Qt 运行库。

## Workbench

题库维护工具 Workbench 独立于本仓库开发，源码暂未随本仓库发布。

Workbench 通过 `--root <游戏目录>` 或设置里的「项目根目录」读取游戏目录下的
`assets/data/questions.nteq`、`assets/data/media.ntep` 和 `assets/map/tiles`。

## 许可证

本项目代码按 **GNU GPL v3.0** 发布，完整文本见 [LICENSE](LICENSE)。

Qt、FFmpeg、MinGW 运行时的许可说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，相关许可证全文在 [LICENSES/](LICENSES/)。

《异环》游戏截图、地图和其他游戏素材的版权归 Hotta Studio / 完美世界所有，不适用本项目的 GPL 许可；本项目为非商业同人作品，素材仅用于非商业用途。
