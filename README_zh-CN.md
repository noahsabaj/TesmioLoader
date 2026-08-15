[English](README.md) | [Русский](README_RU.md) | [简体中文](README_zh-CN.md)

[Changelog](changelog.md) | [Журнал обновлений](changelog_ru.md)

# tesmioloader — Noah Sabaj 的分支

一个用于《Workers & Resources: Soviet Republic》的模组加载器。它替你启动游戏，并在
游戏启动的过程中于内存中打补丁：新的资源、新的矿脉、新的建筑、改动过的规则。它不会
修改游戏自带的任何文件，所以 Steam 的文件完整性校验不会有任何意见。

这是一个个人分支，已与上游解耦，由 Noah Sabaj 独立维护。它走自己的路——自己的插件、
自己的文档、自己的发布节奏——并不跟随上游的分支。

这个版本所打的每一个地址都属于同一个游戏版本：**v1.1.1.9**。启动器会在游戏进程存在
之前先从 `SOVIET64.exe` 中读出版本号，其他版本一律拒绝启动：明知打不了补丁却仍然自信
注入，只是把崩溃推迟到更晚而已。在 `tesmioloader.ini` 中设置 `version_check = 0`，
或使用 `--ignore-version` 参数，可以把这个拒绝重新变回一句警告——留给正在向新版本移植
的人。

## 致谢

基于 **Tesmio 的 TesmioLoader** — https://github.com/MaxLegend/TesmioLoader。
注入、VFS、钩子，乃至“这款游戏可以通过代码来修改”这个想法本身，都是他的成果；没有他，
就没有这个分支。这是他本人的链接，如果你想支持他或看看他接下来在做什么：Boosty
https://boosty.to/tesmio/donate 与 YouTube https://www.youtube.com/@tesmio。

## 安装

1. 确保 Steam 正在运行。启动器是在其下方启动真正的游戏，并不会取代 Steam。
2. 运行 `tesmioloader\build\tesmiolauncher.exe`——**不是** `SOVIET64.exe`。通过
   Steam 正常启动，运行的是未经修改的游戏。
3. 窗口会显示它找到的游戏路径（它会遍历你的 Steam 库文件夹）；如果路径不对或为空，
   点击 **Browse...**，指向 `SOVIET64.exe` 或它所在的文件夹。
4. 勾选本次游玩想启用的插件，然后按 **Launch**。

插件需要手动安装，而这一步正是大家最容易漏掉的：插件的 `.dll` 及其 `.ini` 要放进
`tesmioloader\build\plugins\`，不能放在别处。在 Steam 创意工坊订阅只会下载文件，并不会
安装它们——你仍然需要自己把文件复制到那个文件夹里。

其余内容都在 `docs/` 中：面向玩家的
[docs/guide/00-getting-started.md](docs/guide/00-getting-started.md)（启动、安全地
编辑 `.ini`、查看日志），以及 [docs/01-architecture.md](docs/01-architecture.md)
起的各章，讲的是它内部如何工作（这些章节仅有英文版）。

## 插件

每一项功能都是一个独立的 DLL。要去掉某项功能，删掉它的文件即可。完整列表以及每个插件
对应的文档页，见 [docs/09-plugins.md](docs/09-plugins.md)。

## 许可证

GNU GPL v3。完整文本见本仓库中的 [LICENSE](LICENSE) 文件。
