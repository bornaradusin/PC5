Your installed games are in PC5's library, a row of covers sorted by name. From there you start a game, open its menu to set it up, and come back to PC5 when you leave it. This page covers the library, starting and leaving a game, the game menu and frame generation.

## The library

![The PC5 library](images/library.png)
*The library. Under the chosen game: its name, 32-bit or 64-bit, what it draws with, and its size.*

| Button | In the library |
| --- | --- |
| Left / right, or the left stick | Move between games |
| L1 / R1 | Jump to the first game of the previous or next letter |
| ✕ | Play the chosen game |
| △ | The chosen game's menu |
| Options | Settings |

If the console is set to confirm with **○**, PC5 follows it: **○** plays and chooses, and **✕** goes back.

When PC5 opens, the game you played last is chosen. The library holds up to 128 games. The clock at the top right is the console's.

### Using a USB keyboard in PC5

A USB keyboard works in PC5's own screens too:

| Key | Does what |
| --- | --- |
| Arrow keys | Move |
| Enter or Space | ✕ (choose) |
| Escape or Backspace | ○ (back) |
| F1, Tab or the menu key | Options |
| F2 or Delete | □ |
| F3 | △ |
| Page Up / Page Down | L1 / R1 |

## Starting a game

1. Choose the game in the library.
2. Press **✕** (or choose **Play** in its menu).

PC5 shows a loading screen with the game's name until the game shows its first picture.

![The loading screen](images/loading-screen.png)
*The loading screen, shown until the game draws its first picture.*

Good to know:

- Each game's Windows language is the PS5's language, so a game that follows the Windows language follows the console.
- PC5's [settings](Settings) (overlay, vibration, light bar, triggers) and the game's own settings (controller, frame generation, [launch options](Launch-options)) are read each time the game starts. A change takes effect the next time you start it.

## Leaving a game

Hold the **touchpad** and **Options** together for **two seconds**.

1. The controller gives a short buzz: the hold was taken.
2. PC5 asks the game to close (as Alt+F4 does on a PC), so it can save.
3. If the game hasn't closed after five seconds, it is ended.
4. You are back in PC5. If you started the game from its [home screen tile](Home-screen-tiles), you are back on the home screen.

This works in every controller mode and can't be remapped. Don't use it to close a pop-up in the game: it always leaves the game. See [a game's Windows pop-ups](Controllers#a-games-windows-pop-ups) for those.

If the game crashed or closed before showing anything, PC5 tells you when it opens again, and keeps the log. See [Reporting a problem](Reporting-a-problem).

## The game menu

Choose a game in the library and press **△**.

![A game's menu](images/game-menu.png)
*The game menu. Its settings are kept for this game only.*

| Row | What it does |
| --- | --- |
| Play | Starts the game. |
| Controller | What the game sees: **Xbox controller** (the default), **DualSense**, or **Keyboard & mouse**. Press **✕** or left / right to change it. See [Controllers](Controllers). |
| Button mapping | Only with Keyboard & mouse: what each button and stick sends. See [Controllers](Controllers#button-mapping). |
| Frame generation | **Off** (the default) or **30 → 60 fps**. See [Frame generation](#frame-generation) below. |
| Resolution | Only for games drawn with Direct3D, Vulkan or OpenGL: **1080p** (the default), **1440p** or **4K**. See [Resolution](#resolution) below. |
| Memory | Only for 32-bit games: **4 GB** (the default since 1.4.0) or **2 GB**. A 32-bit game gets 4 GB of address space, room for big textures and mods. Set 2 GB for a game that misbehaves with it. |
| Change program | The program PC5 starts for this game ("Now: …"). Choose another one if PC5 picked the wrong one. See [Choosing the right program](Installing-games#choosing-the-right-program). |
| Home screen tile | **Off** or **On**: a tile on the PS5's home screen that starts this game. See [Home screen tiles](Home-screen-tiles). |
| Uninstall | Deletes the game with its saves and settings, after asking. See [Uninstalling a game](Installing-games#uninstalling-a-game). |

Press **○** or **△** to close the menu.

## Frame generation

Frame generation shows a game at 60 fps when it runs at 30. It is set for each game: **△ › Frame generation**, **Off** by default.

![Frame generation turned on in a game's menu](images/game-menu-frame-generation.png)
*Frame generation on: the game is held at 30 fps and shown at 60.*

How it works: the game is held at 30 fps, and before each of its frames PC5 shows one made between it and the one before, with [ps5-framegen](https://github.com/Swordpdf/ps5-framegen)'s interpolator (AMD FSR3's optical flow).

What to expect:

- It suits games that keep 30 fps or a little more. When a game falls below 30, its frames are shown as they come, without frames in between, until it keeps up again.
- Frames made in between can show smearing around fast movement and at the screen's edges.
- The game reacts a little later than without it.
- The overlay's FPS counts every frame shown, the made ones too, so it shows about 60.
- It works with games drawn with Direct3D 8 to 12, Vulkan or OpenGL. It doesn't apply to DirectDraw or plain Windows drawing, nor to 10-bit (HDR) output.
- A change takes effect the next time the game starts.

## Resolution

A game drawn with Direct3D, Vulkan or OpenGL can be drawn at **1080p** (the default), **1440p** or **4K**: **△ › Resolution**, kept for that game.

- The console shows 1080p and 4K pictures as they are, and a 1440p one scaled up to 4K.
- A higher resolution is slower: 1440p is nearly twice the work of 1080p, and 4K four times. A game that is already slow at 1080p gets slower.
- Games drawn with DirectDraw or plain Windows drawing are drawn at 1080p, so the row isn't shown for them.
- A change takes effect the next time the game starts.

## .NET games

.NET Framework games and tools (XNA through FNA, MonoGame, WinForms) run with [Wine Mono](https://gitlab.winehq.org/mono/wine-mono), the open-source stand-in for Microsoft's .NET Framework that Wine itself uses.

- The first time a .NET game starts, PC5 shows **Setting up .NET for your games, once** while it copies the runtime (some 230 MB) to `/data/prospero-win/pc5/`. Every .NET game uses that one copy, so the next ones start straight away.
- A .NET game installed with an older PC5 gets what it needs the next time it starts.
- It isn't Microsoft's .NET: programs that mix .NET and C++ (C++/CLI) don't run, and some WPF doesn't.

## The overlay

PC5 can show an overlay over every game: FPS, frame time, CPU usage, CPU and SoC temperatures, and memory. Turn it on in **Settings › Overlay**. See [Settings](Settings#overlay).

## When a game doesn't start

If PC5 can't start a game, it says why: for example "*Game* can't start" with the reason. See [Troubleshooting](Troubleshooting) for PC5's messages and what to do about them.

Games installed in `/data/prospero-win` some other way (not with PC5) show up in the library too, and get a cover the first time PC5 opens. Their program is set in their prospero-win profile, so **Change program** doesn't apply to them.
