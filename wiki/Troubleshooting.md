When something doesn't work, PC5 usually says why in a message. This page lists PC5's messages and common problems, grouped by what you were doing, with what to do about each. If your problem isn't here, or the steps don't help, [report it](Reporting-a-problem) with the game's log.

## Installing games

| PC5 says | What to do |
| --- | --- |
| No game folders found | Put each game's installed folder (the one with its `.exe` in it) at the top of a USB drive formatted as exFAT, or in a `Games` folder there. Plug the drive into the PS5 and press **△** (Look again). |
| PC5 can't read that folder | Check that the USB drive is still plugged in, then try again. |
| There is no Windows program in this folder | The folder has no `.exe` PC5 can start, up to six folder levels deep. Copy the game as it is installed on a PC. |
| This folder only has installers and tools, not a game | The folder holds a setup program, not an installed game. Install the game on a PC first, then copy the installed folder. |
| Windows can't use the name "…" | Rename that file or folder on the USB drive. Windows doesn't accept `< > : " \| ? * \`, names ending in a dot or a space, or names such as `CON`, `NUL`, `COM1` or `LPT1`. |
| A path in the folder is too long, or The folder goes too many levels deep | Shorten the folder and file names, or move the game's folder up to the top of the drive. |
| The game's program is too deep in its folder for Windows | The path to the `.exe` is too long for Windows. Shorten the game's folder name. |
| Not enough space | PC5 needs the game's size plus about 1.5 GB free. Free up space on the console, or uninstall a game you don't play. |
| Not enough space for all of them | Go back and untick some games, then install the rest. |
| Nothing to install | None of the ticked folders has a game PC5 can install. Check each folder as above. |
| The copy of … doesn't match the USB drive | A file read back from the console differs from the USB drive. The drive or its cable may be failing. Nothing was kept: try again with another drive or USB port. |
| The USB drive stopped answering while PC5 copied … | The drive was unplugged or stopped working. Plug it in again and install again. |
| PC5's Windows template is missing from the app, or is damaged | The app's folder is incomplete. Copy the `PPSA11018` folder to the console again. See [Updating PC5](Installing-and-updating#updating-pc5). |
| This is a .NET program, and PC5 has no .NET yet: it may not start | A warning on the review screen. .NET Framework games don't run yet, as prospero-win has no .NET. See [What runs and what doesn't](Installing-games#what-runs-and-what-doesnt). |

## Starting a game

| PC5 says | What to do |
| --- | --- |
| *Game* can't start: Its Windows folder is missing | The game's files are gone or incomplete. Uninstall it from its menu (**△**) and install it again. |
| *Game* can't start: Its profile has a mistake | The game's prospero-win profile can't be read. If PC5 installed the game, uninstall it and install it again. |
| *Game* can't start: PC5's notes on this game are missing | Uninstall the game from its menu (**△**) and install it again. |
| *Game* didn't start: The console refused to start it | Try again. If it happens again, close PC5 and open it again. |
| *Game* closed before showing anything | PC5 may have started the wrong program: use **△ › Change program**. If you added [launch options](Launch-options), try without them. Otherwise, [report it](Reporting-a-problem). |
| *Game* stopped with an error | The game crashed. Try the newest PC5, then [report it](Reporting-a-problem) with the log PC5 names. |
| *Game* had to be closed | The game didn't close when asked, so PC5 ended it. It may not have saved. |
| PC5 ended unexpectedly last time | PC5 itself stopped. [Report it](Reporting-a-problem) with the log PC5 names. |

## Playing a game

| Problem | What to do |
| --- | --- |
| The game doesn't react to the controller | Change its controller mode: **△ › Controller**. Try **DualSense** if **Xbox controller** doesn't work, or **Keyboard & mouse** for a game without controller support. See [Controllers](Controllers). |
| A Windows box appears before the game starts | With Xbox controller or DualSense, press **✕** to press its highlighted button, and use the D-pad to move between buttons. See [Controllers](Controllers#a-games-windows-pop-ups). |
| A setting I changed did nothing | Settings and a game's controller, frame generation and launch options take effect the next time the game starts. Leave the game and start it again. |
| The game runs below 30 fps with frame generation on | Frame generation only makes frames in between while the game keeps 30 fps. Below that, frames are shown as they come. See [Frame generation](Playing-games#frame-generation). |
| The overlay is missing a line | A line the console can't read is left out. |
| The light bar or triggers don't follow PC5's settings | In **DualSense** mode, a game that sets its own light bar or trigger effects gets them instead. |
| I can't leave the game | Hold the **touchpad** and **Options** together for a full two seconds. The controller buzzes when the hold is taken. |

## Changing a game

| PC5 says | What to do |
| --- | --- |
| This game has one program | There is no other program in its folder to start. |
| PC5 didn't install this game | The game was installed some other way. Its program is set in its prospero-win profile, so PC5 can't change it. |
| PC5 can't read the game's folder | Its files may be missing: uninstall it and install it again. |
| *Game* wasn't fully uninstalled | PC5 finishes deleting it the next time it starts. |

## Covers

| Problem | What to do |
| --- | --- |
| The wrong cover was found | When installing, press **L1** / **R1** on the review screen to choose another. For an installed game, use [your own picture](Covers#for-a-game-already-installed). |
| My own cover isn't used | Check the folder (`/data/prospero-win/covers/`), the name (the game's folder name, its name in PC5 or its id) and the extension (`.jpg`, `.jpeg` or `.png`). The picture must be newer than the game's current cover. Then close PC5 and open it again. |
| A game has a plain card as its cover | No source had the game, or the console was offline when it was installed. Use [your own picture](Covers#using-your-own-cover). |
| The home screen tile shows the old cover | Turn the tile off and on again: **△ › Home screen tile**. |

See also [Home screen tiles](Home-screen-tiles#if-something-goes-wrong) for problems with tiles.
