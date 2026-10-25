PC5 keeps everything where prospero-win keeps it, in `/data/prospero-win` on the console. The app's own folder (`PPSA11018`) holds only the program, so replacing it to update PC5 leaves your games, saves and settings alone. This page lists what is where, for when you want to back up saves, add a cover or launch options, or find a log.

## The folders

```text
/data/prospero-win/
  profiles/<game>.profile                   what prospero-win starts (written by PC5)
  prefixes/<game>/                          the game's own Windows: registry, saves, settings
  prefixes/<game>/drive_c/Games/<folder>/   the one copy of the game
  prefixes/<game>/pc5/                      PC5's notes on the game, its controller, its cover
  prefixes/<game>/pc5-cache/radv/           its shader cache
  covers/<name>.jpg / .png                  your own covers (you make this folder)
  pc5/settings.txt                          PC5's settings
  pc5/last.txt                              the game played last
  pc5/update.txt                            the last update check
  logs/                                     session logs (see Reporting a problem)

/data/homebrew/PPSA99…/                     a game's home screen tile
```

- `<game>` is the game's id: the name of its folder on the USB drive, in lower case, with spaces and other signs turned into dashes (`neon-drift`). The easiest way to find it is to look in `prefixes/` for the folder named like the game.
- `<folder>` is the game's folder as it was on the USB drive (`Neon Drift`).

## In detail

| Path | What it is | Can you change it? |
| --- | --- | --- |
| `prefixes/<game>/` | The game's own Windows. Saves and settings a game writes to "Documents" or "AppData" are in here. | Copy it to back up saves. Don't delete it by hand: [uninstall](Installing-games#uninstalling-a-game) the game instead. |
| `prefixes/<game>/drive_c/Games/<folder>/` | The game's files, as copied from the USB drive. Saves a game writes into its own folder are here too. | You can add a [`launch.txt`](Launch-options) beside the game's `.exe`. |
| `prefixes/<game>/pc5/` | PC5's notes on the game (`info.txt`), its controller and frame generation (`controls.txt`), its cover (`cover.bin`) and the picture as downloaded. | Change these in PC5 (the game's menu), not by hand. |
| `profiles/<game>.profile` | The game's prospero-win profile. | No: PC5 writes it again each time the game starts. Use `launch.txt` for launch options. |
| `covers/` | Your own covers for installed games. | Yes. See [Covers](Covers#for-a-game-already-installed). |
| `pc5/settings.txt` | PC5's [Settings](Settings). | Change them in PC5. A missing file, or a line PC5 doesn't know, gives the default. |
| `logs/` | Session logs: `session-N.log` while running, then `issue-…` or `log-…` files, kept for seven days. | Copy them off to [report a problem](Reporting-a-problem). |

## Backing up a game's saves

Uninstalling a game deletes its saves, with no backup. To keep them:

1. Copy the game's folder `/data/prospero-win/prefixes/<game>/` off the console, with the tool you use to move files to and from the console.
2. A game keeps its saves either in its Windows folders (such as `drive_c/users/…`) or in its own folder under `drive_c/Games/<folder>/`. Keeping the whole `prefixes/<game>/` folder covers both.

## Games installed some other way

Games already in `/data/prospero-win` that PC5 didn't install show up in PC5's library too, and get a cover the first time PC5 opens. Their program is set in their prospero-win profile, so **Change program** doesn't apply to them.

## Cut-short installs and uninstalls

An install copies the game into a folder named `prefixes/.installing-…` first, and an uninstall moves the game to `prefixes/.removing-…` before deleting it. If the console is switched off in the middle, PC5 finishes the job by deleting that folder the next time it starts. It says "Finishing an install or uninstall that was cut short" while it does.
