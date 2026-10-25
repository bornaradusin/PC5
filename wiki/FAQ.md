Short answers to questions people often ask about PC5, with links to the pages that explain more.

## General

**Does PC5 come with games?**
No. PC5 contains no games and downloads none. You install the games you own, from a USB drive.

**Which games run?**
Whether a game runs is only known by trying it. PC5 makes no promise about any game. Games that need Steam, Epic, EA or Ubisoft's launcher running, online DRM or anti-cheat don't run, and nor do .NET Framework games. See [What runs and what doesn't](Installing-games#what-runs-and-what-doesnt).

**Is there a list of games that work?**
No. Try the game: if it doesn't run, uninstalling it removes everything it made.

**Does PC5 need internet?**
No. PC5 itself only goes online to find covers and to check for a newer version. Without internet, games get a cover made from their icon, or [your own picture](Covers#using-your-own-cover).

**Do I need a Steam, GOG or other account?**
No. Covers come from public sources with no account and no key, and PC5 doesn't run any store's launcher.

**How many games can PC5 hold?**
Up to 128, as long as the console has the space.

**What does PPSA11018 mean?**
It is PC5's title ID: the name of the app's folder and of the release's zip file.

## Installing

**Can I install from the console's own storage or the network?**
No. PC5 installs games from USB drives only, formatted as exFAT. See [Installing games](Installing-games).

**Do I need to keep the USB drive plugged in to play?**
No. PC5 copies each game to the console. The USB drive is only read while installing, and never written to.

**Can I install a game's installer (setup.exe)?**
No. Install the game on a PC first, then copy the installed folder to the USB drive.

**PC5 picked the wrong .exe. What now?**
Choose the game, press **△** and use **Change program**. See [Choosing the right program](Installing-games#choosing-the-right-program).

**What happens if the console switches off during an install?**
The unfinished copy is deleted the next time PC5 starts. Install the game again.

## Playing

**How do I leave a game?**
Hold the **touchpad** and **Options** together for two seconds. See [Leaving a game](Playing-games#leaving-a-game).

**Can I use a keyboard and mouse?**
Yes. A USB keyboard and mouse work in every controller mode. For games without controller support, set the game's controller to **Keyboard & mouse** and map the buttons to keys. See [Controllers](Controllers).

**Can I make a game run at 60 fps?**
For a game that keeps 30 fps, turn on **△ › Frame generation**. It shows the game at 60 fps with frames made in between. See [Frame generation](Playing-games#frame-generation).

**Which language do games use?**
The PS5's. Each game's Windows language is the console's, so a game that follows the Windows language follows the console.

**Can I start a game straight from the home screen?**
Yes, with a [home screen tile](Home-screen-tiles). It needs ShadowMountPlus and elfldr.

## Saves and updates

**Where are my saves?**
In the game's own Windows folder, `/data/prospero-win/prefixes/<game>/`. See [Files and folders](Files-and-folders).

**Does uninstalling a game delete its saves?**
Yes. Uninstalling deletes the game with its saves, settings, shader cache, cover and home screen tile's folder. There is no backup and no undo. [Back up the saves](Files-and-folders#backing-up-a-games-saves) first if you want to keep them.

**Do I lose my games when I update PC5?**
No. Games, saves and settings are in `/data/prospero-win`, not in the app's folder. Replace the app's folder and everything stays. See [Updating PC5](Installing-and-updating#updating-pc5).

**Do I need to reinstall games after updating PC5?**
No. When a new version brings something games need, each game gets it the next time it starts.

**How do I know a new version is out?**
With **Settings › Updates › Check when PC5 opens** on (the default), PC5 asks GitHub when it opens and shows a notification when a newer version is out.

## Problems

**A game crashed. What should I do?**
Update to the newest PC5 and try again. If it still crashes, [report it on GitHub](Reporting-a-problem) with the log PC5 kept.

**Where do I report a problem?**
As an issue on GitHub, <https://github.com/bornaradusin/PC5/issues>, with the game's log attached. Not in ProsperoStore comments. See [Reporting a problem](Reporting-a-problem).
