PC5 keeps everything where prospero-win keeps it, in `/data/prospero-win` on the console. The app's own folder (`PPSA11018`) holds only the program, so replacing it to update PC5 leaves your games, saves and settings alone. This page lists what is where, for when you want to back up saves, add a cover or launch options, or find a log.

## The folders

```text
/data/prospero-win/
  profiles/<game>.profile                   what prospero-win starts (written by PC5)
  prefixes/<game>/                          the game's own Windows: registry, saves, settings
  prefixes/<game>/drive_c/Games/<folder>/   the one copy of the game
  prefixes/<game>/pc5/                      PC5's notes on the game, its controller, its cover and background
  prefixes/<game>/pc5-cache/radv/           its shader cache
  covers/<name>.jpg / .png                  your own covers (you make this folder)
  backgrounds/<name>.jpg / .png             your own backgrounds (you make this folder)
  loading/<name>.jpg / .png                 your own loading pictures (you make this folder)
  sounds/<name>.wav                         your own startup sounds (you make this folder)
  saves/<game>/                             a game's saves, kept when it was uninstalled
  pc5/settings.txt                          PC5's settings
  api_keys.txt                              your API keys, such as SteamGridDB's (you fill it in)
  pc5/last.txt                              the game played last
  pc5/update.txt                            the last update check
  logs/                                     session logs (see Reporting a problem)

/data/homebrew/PPSA99…/                     a game's home screen tile, an app of its own

PPSA11018/                                  the app (where you installed it)
  pc5/manifest.txt                          the list of the app's files, for updates
  INSTALL/                                  games waiting to be installed (see Installing games)
```

- `<game>` is the game's id: the name of its folder on the USB drive, in lower case, with spaces and other signs turned into dashes (`neon-drift`). The easiest way to find it is to look in `prefixes/` for the folder named like the game.
- `<folder>` is the game's folder as it was on the USB drive (`Neon Drift`).

## In detail

| Path | What it is | Can you change it? |
| --- | --- | --- |
| `prefixes/<game>/` | The game's own Windows. Saves and settings a game writes to "Documents" or "AppData" are in here. | Copy it to back up saves. Don't delete it by hand: [uninstall](Installing-games#uninstalling-a-game) the game instead. |
| `prefixes/<game>/drive_c/Games/<folder>/` | The game's files, as copied from the USB drive. Saves a game writes into its own folder are here too. | You can add a [`launch.txt`](Launch-options) beside the game's `.exe`. |
| `prefixes/<game>/pc5/` | PC5's notes on the game (`info.txt`), its controller and frame generation (`controls.txt`), its cover (`cover.bin`) and its background (`background.bin`). | Change these in PC5 (the game's menu), not by hand. You can add an [`env.txt`](Launch-options#a-games-environment-variables-envtxt) here for a game's environment variables, and a `winedebug.txt` when an issue asks for a more detailed log: see [Logs and debugging](Logs-and-debugging#a-more-detailed-log-for-one-game). |
| `profiles/<game>.profile` | The game's prospero-win profile. | No: PC5 writes it again each time the game starts. Use `launch.txt` for launch options. |
| `PPSA11018/INSTALL/` | Games you put there over FTP, from the web page or with the file manager, for PC5 to install as it opens. | Yes. See [The INSTALL folder](Installing-games#the-install-folder). |
| `covers/` | Your own covers for installed games. | Yes. See [Covers](Covers#for-a-game-already-installed). |
| `backgrounds/` | Your own backgrounds for installed games. | Yes. See [Backgrounds](Covers#backgrounds). |
| `loading/` | Your own loading pictures: `loading.jpg` or `.png` for every game, or one named after a game. | Yes. See [Loading screen](Covers#loading-screen). |
| `sounds/` | Your own startup sounds. | Yes. See [Settings](Settings#startup-sound). |
| `saves/<game>/` | A game's Windows (its saves, settings, registry and options) without the game or its pictures, kept by **Keep saves** when it was uninstalled. Installing the game again can put it back. | Delete it in **Options › Game maintenance › Uninstall game**. See [Keeping a game's saves](Installing-games#keeping-a-games-saves). |
| `api_keys.txt` | Your API keys, as `NAME=value` lines: `STEAMGRIDDB_API_KEY` for [SteamGridDB](Covers#steamgriddb). PC5 makes it, with the keys empty, the first time it opens, and never deletes it. | Yes: put your key in it. PC5 never shows a key or logs it. |
| `pc5/settings.txt` | PC5's [Settings](Settings). | Change them in PC5. A missing file, or a line PC5 doesn't know, gives the default. |
| `PPSA11018/pc5/manifest.txt` | In the app's own folder (from 1.9.0): every file of the release, with its size and SHA-256. PC5's updater compares it with the new release's list to download only the files that changed. | No. If it is missing or a file differs from it, PC5 downloads the whole app when it updates. |
| `logs/` | Session logs: `session-N.log` while running, then one `issue-….zip` or `log-….zip` per session (with the game's own logs and crash files), kept for seven days. | Copy the zip off to [report a problem](Reporting-a-problem). |

## Backing up a game's saves

Uninstalling a game with **Delete all** deletes its saves, with no backup; **Keep saves** keeps the ones in its Windows on the console (see [Keeping a game's saves](Installing-games#keeping-a-games-saves)). To keep a copy off the console:

1. Copy the game's folder `/data/prospero-win/prefixes/<game>/` off the console, with the tool you use to move files to and from the console.
2. A game keeps its saves either in its Windows folders (such as `drive_c/users/…`) or in its own folder under `drive_c/Games/<folder>/`. Keeping the whole `prefixes/<game>/` folder covers both.

## Games installed some other way

Games already in `/data/prospero-win` that PC5 didn't install show up in PC5's library too, and get a cover the first time PC5 opens. Their program is set in their prospero-win profile, so **Change program** doesn't apply to them.

## Cut-short installs and uninstalls

An install copies the game into a folder named `prefixes/.installing-…` first, and an uninstall moves the game to `prefixes/.removing-…` before deleting it. If the console is switched off in the middle, PC5 finishes the job by deleting that folder the next time it starts. It says "Finishing an install or uninstall that was cut short" while it does.

An update copies the newer version into `prefixes/.updating-<game>/` first. Switched off during the copy, the copy is deleted and the game stays as it was; switched off while the new files are put in place, PC5 finishes putting them in place. Keeping a game's saves moves its Windows to `saves/.keeping-<game>/` first, and is finished the same way.
