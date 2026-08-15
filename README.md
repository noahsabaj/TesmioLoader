[English](README.md) | [Русский](README_RU.md) | [简体中文](README_zh-CN.md)

[Changelog](changelog.md) | [Журнал обновлений](changelog_ru.md)

# tesmioloader — Noah Sabaj's fork

A mod loader for *Workers & Resources: Soviet Republic*. It starts the game for
you and, while the game is starting, patches it in memory: new resources, new
deposits, new buildings, changed rules. It never modifies a file the game came
with, so Steam's file verification has nothing to complain about.

This is a personal fork, decoupled from upstream and maintained independently by
Noah Sabaj. It goes its own way — its own plugins, its own docs, its own release
pace — and it does not track upstream's branches.

Every address this build patches belongs to one build of the game: **v1.1.1.9**.
The launcher reads the version out of `SOVIET64.exe` before the process exists
and refuses to launch anything else, because injecting confidently into a game
you cannot patch only produces a crash further down the road. `version_check = 0`
in `tesmioloader.ini`, or `--ignore-version`, turns that refusal back into a
warning for whoever is porting to a new build.

## Credit

Built on **Tesmio's TesmioLoader** — https://github.com/MaxLegend/TesmioLoader.
The injection, the VFS, the hooking, the whole idea that this game can be modded
through code at all: that is his work, and this fork does not exist without him.
His own links, if you want to support him or watch what he builds next: Boosty
https://boosty.to/tesmio/donate and YouTube https://www.youtube.com/@tesmio.

## Install

1. Make sure Steam is running. The launcher starts the real game underneath; it
   does not replace Steam.
2. Run `tesmioloader\build\tesmiolauncher.exe` — **not** `SOVIET64.exe`. Starting
   the game the normal way through Steam runs the unmodified version.
3. The window shows the game path it found (it looks through your Steam library
   folders); if it is wrong or empty, click **Browse...** and point it at
   `SOVIET64.exe` or the folder holding it.
4. Tick the plugins you want this session, then press **Launch**.

Plugins install by hand, and this is the step people miss: a plugin's `.dll` and
its `.ini` go in `tesmioloader\build\plugins\` and nowhere else. Subscribing to
an item on the Steam Workshop downloads the files, it does not install them —
you still have to copy them into that folder yourself.

The rest is in `docs/`: [docs/guide/00-getting-started.md](docs/guide/00-getting-started.md)
for players (launching, editing an `.ini` safely, reading the log), and
[docs/01-architecture.md](docs/01-architecture.md) onward for how it works
inside.

## Plugins

Every feature is a separate DLL. Removing one is deleting its file. The full
list, with a doc page for each, is in [docs/09-plugins.md](docs/09-plugins.md).

## License

GNU GPL v3. The full text is in [LICENSE](LICENSE) in this repository.
