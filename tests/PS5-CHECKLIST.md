# Testing PC5 on a PS5

What to try on a console, with what to look for on screen and in the log. A session's log is
`/data/prospero-win/logs/session-N.log` while it runs; when PC5 next opens it deletes the
logs of sessions that went well and keeps the others as `logs/issue-<when>_<game>.log`. To
keep every log while testing, set Settings › Keep all logs to On first. PC5's lines start
with `PC5 `.

## PC5

| # | Do | Expect on screen | Expect in the log |
| --- | --- | --- | --- |
| 1 | Replace the `PPSA11018` folder with the new one, start PC5 | the tile shows the tower without lettering; the library | `PC5 start version=1.2.1 …`, `PC5 controller reset light=0 triggers=0` |
| 2 | Options › About PC5 | version 1.2.1 and the free space | |
| 3 | Options › Updates | "No PC5 release is published on GitHub yet" (until there is one) | `PC5 update state=2` (2: none published) |
| 4 | Options › In games: change Vibration, Light bar and Adaptive triggers; Options › Overlay: turn Show FPS on, pick a corner | the values change and stay after restarting PC5; Trigger rumble / Trigger resistance appear only with their effect on | `PC5 settings fps=1 …`; `/data/prospero-win/pc5/settings.txt` |
| 5 | Options › Game maintenance › Install games from USB, tick two folders with □, press Options | "Getting ready 1 of 2", then the list with covers | `PC5 folder=… queue=1/2`, `PC5 cover choices=… queue=…` |
| 6 | ✕ on one, L1 / R1, ✕ (Done) | another cover; back to the list | |
| 7 | Options (Install all) | "Game 1 of 2", "Step 1 of 3 …" with MB/s on every step, then a summary | `PC5 install id=… queue=1/2`, `PC5 queue done installed=2 of=2` |
| 8 | Install again, press ○ during the copy, choose Stop this game | that game stops, the next one installs | `PC5 install ended cancelled=1` |
| 9 | △ on a game › Change program | its programs; choosing one says "… starts … now" | `PC5 program id=… exe=…` |

## In a game

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 10 | Start GTA IV | green falling code with "Grand Theft Auto IV", then the game | `PC5 game fps=… tint=…`, `PW_WINE64 profile … dll_overrides=d3d8,d3d9,d3d10core,d3d11,dxgi=n;d3d12,d3d12core=b`, no `Unhandled page fault` |
| 11 | With Show FPS on | the counter in the chosen corner, once the game shows its picture | `PW_WINE64 pc5 vulkan hud=1 stats=1 tick=1 color=1` |
| 11b | Overlay: every line on, then start a game | FPS, MS, CPU %, CPU °C, SOC °C and RAM used/total in the chosen corner, updated every second; a line the console can't read is left out | `PC5 overlay lines=0x3f cpus=16 cpu_temp=… rc=0 soc_temp=… rc=0 memory=…M/…M rc=0` |
| 12 | Light bar: Game's colour, then Follows the screen | the cover's colour; then the screen's, changing with it | `PC5 light bar … rc=0` |
| 13 | Adaptive triggers: Rumble, then Resistance, then Rumble + resistance | L2/R2 buzz when the game rumbles; then push back; then push back and buzz while it rumbles | `PC5 triggers rumble mode=2 rc=0`, `PC5 triggers resistance mode=1 rc=0` |
| 14 | Trigger rumble and Trigger resistance at 25 %, then 100 % | a faint buzz and a light push at 25 %; strong at 100 % | `PC5 game … trigger_rumble=25 trigger_resistance=25 …` |
| 14b | Vibration 25 %, then Off | weaker rumble; then none | `PC5 game … vibration=25 …` |
| 15 | Hold the touchpad and Options for 2 seconds | a short buzz, the game closes (or is ended after 5 s), PC5 shows | `PW_WINE64 close requested by=combo` |
| 16 | A game that closes at once (or crashes) | PC5, when it opens, says so and names the kept log | `PC5 logs kept=1 newest=…` |
| 16b | Start Unrailed 2: Back on Track | it gets past its start to its menu | no `GetComputerName` failure |
| 16c | Samurai Warriors 4 DX: play a stage for 15 minutes | the sound plays on and the game doesn't crash | no `Unhandled page fault` |

## Controllers

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 17 | △ on a game › Controller: Xbox controller, DualSense, Keyboard & mouse | the row changes, and stays after restarting PC5; Button mapping shows only with Keyboard & mouse | `prefixes/<game>/pc5/controls.txt` has `mode = …` |
| 18 | Keyboard & mouse › Button mapping: give Cross a key, move the pointer to the left stick, change its speed, start the game | the key, the pointer and its speed in the game; Reset to default brings W A S D back | `PW_WINE64 profile … input=keyboard …` |
| 19 | DualSense, then start a game with DualSense or SDL support (or a DirectInput game) | the game finds a controller and shows PlayStation buttons (an SDL game); the buttons, sticks and triggers work | `PW_WINE64 display … xinput=0 dualsense=1`, `PW_WINE64 dualsense output=1 motion=ok rc=0`, `xinput: the controller is a DualSense (054c:0ce6, …)` |
| 20 | In that game: something that rumbles, changes the light bar or sets the triggers | the motors at Vibration's strength; the game's colour on the light bar; the game's trigger effects | `PW_WINE64 dualsense output set=…`, `PC5 light bar the game's …`, `PC5 triggers game effects=… rc=0` |
| 21 | DualSense: the touchpad and tilting the controller in a game that uses them | the game follows the finger and the tilt | |
| 22 | Xbox controller, then a game that reads controllers through Windows.Gaming.Input (Deep Rock Galactic Survivor, other Unity games) | the game finds the controller: buttons, sticks and triggers work, and its rumble reaches the motors | `PW_WINE64 rumble left=… right=… status=ok` when it rumbles |

## Frame generation

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 23 | △ on a game › Frame generation: 30 → 60 fps, then start a game that keeps 30 fps or more, with Show FPS on | the game moves at 60; the overlay shows about 60 | `PC5 game … frame_generation=1`, `pw_framegen: on, 29.92 fps shown at 59.84`, `pw_framegen: swapchain …x… format …, … images`, then every 10 s `pw_framegen: frames=… generated=… late=… engaged=1` with generated close to frames |
| 24 | In that game: a busy scene where it falls below 30, then back | no stutter beyond the game's own; at 30 again it goes back to 60 | `pw_framegen: frame … ms late; shown as it is`, later `engaged=1` again |
| 25 | Frame generation Off, start the game again | the game at its own rate | `PC5 game … frame_generation=0`, no `pw_framegen:` lines |

## Videos and sound

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 26 | A game installed with PC5 1.1.1 or older, started for the first time with 1.2.1 | it starts as before | `PC5 prefix media registered`, once: not on the next start |
| 27 | A game that plays a video through Windows at its start or in its menus (a `.wmv`, `.mp4`, `.avi` or `.mpg` among its files; not Bink `.bik`, which games decode themselves) | the video plays with its sound, and the game goes on after it | no `Unhandled page fault`; no `couldn't load in-process dll` or `err:` lines for `winegstreamer` or `winedmo` |
| 28 | A game whose music or speech is WMA or MP3 (`.wma`, `.mp3`, `.xwma` among its files) | it plays | no `Unhandled page fault` |

| 29 | A game installed before 1.3.0, started the first time with 1.3.0 | it starts as before | `PC5 prefix DXVK updated`, once: not on the next start; DXVK's log line `DXVK: v2.6.2-prospero2-1-g…` |
| 30 | A game whose movie player shares a picture from Direct3D 9 to Direct3D 11 (a game that loads both `d3d9.dll` and `d3d11.dll`, with `MFReadWrite.dll`) | its movies play | `D3D9: Sharing the Direct3D 11 device's Vulkan device…`; no `Failed to create shared resource` or `OpenSharedResourceGeneric: Handle not found` |

## Home screen tiles and covers

With ShadowMountPlus and elfldr running.

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 31 | △ on a game › Home screen tile | "Added to the home screen", the row says On with the tile's ID; the tile appears on the home screen with the game's cover | `PC5 home tile added …`; `/data/homebrew/PPSA99xxx/forwarder.json` |
| 32 | Start the game from its tile, play, then hold the touchpad and Options | PC5 opens straight into the game (no library); leaving the game goes back to the home screen, not to PC5 | `PW_WINE64 args … tile=<game> exit_after=1`, `PW_WINE64 exit to the home screen: started from a tile` |
| 33 | △ › Home screen tile again (Off), and uninstall a game that has a tile | the tile's folder is gone from `/data/homebrew` (delete the tile on the home screen if it stays) | `PC5 home tile removed …` |
| 34 | Without internet: install a game whose USB folder has a `cover.png` | the review screen offers "your picture" first; the library shows it | `PC5 cover choices=…` |
| 35 | Put `<game's name>.png` in `/data/prospero-win/covers/`, open PC5 | that game's cover becomes the picture, and its tile's picture too if it has one, after the tile is turned on again | |

## Pop-ups

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 36 | Xbox controller or DualSense: a game that shows a Windows warning box before it starts | ✕ presses its highlighted button, the D-pad moves between Yes / No; the game then starts and the controller works in it as before | |
| 37 | In that game after the box: press ✕ and the D-pad during play | the game sees them as controller buttons only (no Enter or arrow keys, e.g. no text box reacting) | |

## Crash report

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 38 | A game that crashes at start (its `issue-…` log has `Unhandled page fault`) | PC5 says it crashed, as before | after `Unhandled …`: `wine: crash report: eax=…` (or `rax=…`), `wine: crash report: at L"…"+0x…` and `wine: crash report: stack+0x… L"<game>.exe"+0x…` lines |

## Drive letters

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 39 | A game that looks up its drive at start (one that crashed with `write access to 0000002C` before 1.3.1) | it gets past the start | no `Unhandled page fault` at that point |

## Memory, threads and logs (1.4.0)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 40 | Any game | it starts as before | `wine-ps5: memory for programs: … MiB of … MiB direct memory free, reported as 16384 MiB`; `wine-ps5: address space for programs: 16 GiB at 0x1000000000, more when needed up to …` |
| 41 | A game that checks for enough memory (one that said "minimum specs" or "not enough memory") | no memory warning | |
| 42 | A 64-bit game that crashed with `map_view anon mmap error Cannot allocate memory` | it goes on past that point | `wine-ps5: address space for programs grows by …`, no `anon mmap error` |
| 43 | A 32-bit game without the large-address flag | it starts as before | `wine-ps5: a 32-bit program without the large-address flag: 4 GiB of address space…` |
| 44 | △ on a 32-bit game › Memory to 2 GB, start it | it starts | `PC5 game … memory_32bit=2gb` and `…: 2 GiB of address space (PW_LARGE_ADDRESS_AWARE=0)` |
| 45 | A game that starts hundreds of threads (one that logged `kqueue failed with error: 24`) | it goes on past that point | no `kqueue failed`; `wine-ps5: threads in the server's table: 256` and more, no `cannot register thread` |
| 46 | Play a game for a while, then close it, and open its kept log (Keep all logs) | the log starts with the session's first lines | `…-start.log` beside it when the session was long; repeated lines as `the line before came N more times` |
| 47 | A game with sound | it sounds as before | `wineps5: stream … opened: 48000 Hz, …`; any `ran short in … of 1000 grains` names a stream the game fed late |
| 48 | A game with Direct3D 9 and 11 movies (one that shares a picture between them) | its movies play | `PC5 prefix DXVK updated` once (a game installed before 1.4.0); `LocalShare: … textures shared within the process (… MiB), … added, … removed` |

## Resolution, .NET, fonts and the game's own DLLs (1.5.0)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 49 | △ on a Direct3D game › Resolution to 1440p, then 4K, starting it each time | its picture is sharper each time, and fills the screen | `PC5 controls … resolution=1440p`, then `resolution=2160p` |
| 50 | A .NET game (XNA/FNA, MonoGame or WinForms), the first time | "Setting up .NET for your games, once" counts up, then the game starts | `PC5 dotnet runtime copy to=…/pc5/mono-11.3.0 entries=…`, `PC5 dotnet runtime copied entries=…` |
| 51 | A second .NET game | it starts without setting up .NET again | no `PC5 dotnet runtime copy` |
| 52 | A .NET game installed before 1.5.0 | it starts | `PC5 prefix .NET updated` once |
| 53 | A game with Japanese, Chinese or Korean text drawn with Windows' fonts (or a launcher in one of those languages) | the text shows, not empty boxes | `PC5 prefix fonts named` once (a game installed before 1.5.0) |
| 54 | A Direct3D 12 game | it starts as before or better (VKD3D-Proton 3.0.1) | `PC5 prefix DXVK updated` once (a game installed before 1.5.0) |
| 55 | A game that carries its own `d3dcompiler_47.dll` or Visual C++ runtime beside its .exe | it starts as before | |

## Typing (1.6.0)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 56 | In a game with a text box (a name, a chat line), put the cursor in it, click the touchpad with two fingers, type "Né 1", choose Done | the PS5's keyboard opens over the game, and "Né 1" appears in the box | `PC5 keyboard module status=0 handle=… init=1 status=1 result=1 term=1 common=…`, `PC5 keyboard open rc=0`, `PC5 keyboard text: 4 characters to type`, `PC5 keyboard typed 4 characters` |
| 57 | Open the keyboard again and cancel it | it closes, nothing is typed, the game carries on | `PC5 keyboard closed: cancelled` |
| 58 | Click the touchpad with one finger (DualSense or Xbox controller mode) | the game gets the click as before | no `PC5 keyboard` line |

## Aspect ratio (1.6.0)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 59 | A 4:3 game drawn with Direct3D (set to a 4:3 resolution such as 1024x768 in its options), △ › Aspect ratio › Stretch, start it | the picture fills the screen, wider than before | `PC5 game … aspect_ratio=stretch`; `wsi/videoout: a 1024x768 swapchain presents stretched to 1920x1080 at (0, 0)` |
| 60 | The same game with Aspect ratio › Keep | bars at the sides, as before | `wsi/videoout: … presents scaled to 1440x1080 at (240, 0)` |
| 61 | A DirectDraw game with a 4:3 picture, Stretch | the picture fills the screen | `PW_WINE64 profile … scaling=2` |

## Launchers (1.6.0)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 62 | A game whose own launcher (a program in its folder) starts the game: set Change program to the launcher, start it, press its Play | the launcher closes and the game starts | `wine-ps5: hand-off to C:\Games\…: this program ends and the title starts it`, `PC5 hand-off: … starts the program its launcher started`, then the game's own session |
| 63 | Close the game, start it again from PC5 | the launcher comes up again | `PC5 play id=…` with the launcher's profile |
| 64 | A game whose crash reporter or updater starts (a name with crash, report, update…) | it isn't started in the game's place | `wine-ps5: … is a helper: not handed off` |

## Resolution and reports (1.6.1)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 65 | A game that loads Direct3D or Vulkan itself (Enshrouded), installed before 1.6.1: open its menu, then start it once and open the menu again | the Resolution row shows after the start | `PC5 profile` with `graphics = dxvk` (or vkd3d) for it |
| 66 | Any session (PC5, then a game) | | `PC5 system firmware=… data_free=… version=…` (the version in Options › About PC5) in both logs |
| 67 | A game with a crash handler of its own that crashes (Grim Dawn 64-bit) | PC5 says the game stopped with an error when it opens next | `wine: … handed to the program's own crash handler` followed by `wine: crash report:` lines |
| 68 | In that message, press △ | a QR code; scanning it opens GitHub's New issue form for PC5, its title and text filled in | `PC5 report log=issue-….zip link=… qr=…` |
| 69 | Copy the zip it names off the console and open it | PC5's log, about.txt, and under game/ the game's own logs and dumps from that session | |
| 70 | Play a game that works, close it, open PC5 | a `log-…_<game>.zip` for it in `logs/`; playing it again leaves only the newer one | |
| 71 | During a game, hold the power button until the console turns off; start it, open PC5 | that game's newest `log-…` (or `issue-…`) zip holds the session up to a few seconds before the power went | its about.txt says the log has no end line |

## Fixes (1.6.2)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 72 | Unzip `PPSA11018.zip` on a Windows PC, copy the folder to the console with a USB drive, then start a .NET game for the first time | the .NET copy finishes and the game starts | `PC5 dotnet runtime copied entries=…`, no `PC5 problem play` |
| 73 | Install a game whose program's name holds a helper's word (Crashlands 2's `Crashlands2.exe`) | it installs, with that program chosen | `PC5 install … exe=Crashlands2.exe` |
| 74 | Start a game installed with 1.6.0 or older | it starts as before | `PC5 prefix system files added …` the first time, nothing the next time |

## Maths under Direct3D 9 (1.6.3)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 75 | A 32-bit Direct3D 9 game that crashed after some minutes in `MSVCR90.dll` (Blur): play it for 15 minutes or more | it doesn't crash | no `crash report: at L"MSVCR90.dll"` |
| 76 | Any 32-bit Direct3D 9 game that ran before | it runs as before: picture, sound, menus | |

## More than one player (1.6.5)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 77 | A game with local multiplayer, set to Xbox controller: start it, then turn on a second controller and sign in with it as another user | the game finds a second controller within a second; both control their own player | `PW_WINE64 display … players=up-to-4`, `PW_WINE64 players signed in: …`, `PW_WINE64 player 2 joined: user … handle=…` |
| 78 | The same, the game rumbling for player 2 | only player 2's controller rumbles, at Settings › In games › Vibration | `PW_WINE64 player 2 rumble left=… status=ok rc=0` |
| 79 | Sign player 2 out while the game runs, then in again | the game loses the second controller, then finds it again | `PW_WINE64 player 2 left: user … signed out`, then `joined` again |
| 80 | The same game set to DualSense, with two controllers | player 1 is the DualSense (PlayStation buttons), player 2 an Xbox controller | `players=up-to-4`, `player 2 joined` |
| 81 | A game set to Keyboard & mouse, with two controllers signed in | only player 1 controls it | `players=1` |
| 82 | With two players in a game, hold the touchpad and Options on player 2's controller, then on player 1's | player 2's does nothing; player 1's leaves the game | `close requested by=combo` once |

## DirectInput (1.6.5)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 83 | A game that reads its controller with DirectInput (Need for Speed: Most Wanted, 2005, without its XInput mod), set to Xbox controller | the controller works in the game and its controls menu: sticks, both triggers, D-pad, buttons | |
| 84 | The same with two controllers signed in | the game lists two "Controller (XBOX 360 For Windows)" | |
| 85 | A game that reads both XInput and DirectInput (most from after 2006) | each controller counts once, not twice | |

## DirectDraw and batch files (1.6.6)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 86 | A DirectDraw game that draws in 3D (Breath of Fire IV): start it | it shows its picture instead of closing after a few seconds | `zink provider=ok … architecture=pe32`, no `Failed to get a GL context` |
| 87 | A 2D DirectDraw game that ran before | it runs as before | `zink provider=ok` |
| 88 | Install a game whose folder has a `Launch.bat` that starts its program with options | the review screen offers `Launch.bat` first, with the program's bits and graphics; the game starts with the batch file's options | `PC5 install … exe=Launch.bat`, the profile's `arguments =` holds them |
| 89 | △ › Change program on that game, choose the game's `.exe`, then the `.bat` again | each starts the game | |

## Updating itself and the startup sound (1.7.0)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 90 | Open PC5 | its sound plays on the controller's speaker | `PC5 startup sound user=… init=0 port=…` (port not negative), then `played=… output=0` |
| 90a | Settings › In games › Startup sound: step through the sounds | each plays as it is chosen; Off plays nothing; the one left chosen plays when PC5 next opens | `PC5 settings … startup_sound=<name>` |
| 90b | Put a `.wav` (44.1 kHz stereo) in `/data/prospero-win/sounds/`, open Settings | it is in the list under its name and plays | |
| 90c | With games installed by 1.7.x, open the library and wait | each game's background fades in behind the covers once found; scrolling through games fades between them | `PC5 backgrounds to look for=…`, `PC5 background game=… from=Steam` (or GOG) |
| 90d | Put `/data/prospero-win/backgrounds/<game name>.jpg`, close and open PC5 | that picture is the game's background | `PC5 background game=… from=your picture` |
| 91 | Start a game, leave it | no sound when PC5's screens are back | no `PC5 startup sound` line in that session |
| 92 | Settings › In games › Startup sound: Off; close PC5 and open it | no sound | no `PC5 startup sound` line |
| 93 | With a newer release on GitHub, open PC5 | it asks: Update now or Later | `PC5 update state=4 latest=… zip=1` |
| 94 | Later | the library; Settings › Updates shows Update now | `PC5 update later` |
| 95 | Update now, then ○ during the download | it stops; PC5 is as it was | `PC5 update stopped: Cancelled.` |
| 96 | Update now, let it finish | the progress bar through download, unpacking and putting the files in place, then "PC5 x.y.z is installed"; ✕ (Restart PC5) starts the new version, with no error from the console | `PC5 update folder=…`, `download rc=0 status=200`, `placed=…`, `registration copies=…`, `done`, `PC5 restarting after its update` |
| 97 | After the restart, Settings › About PC5; then close PC5 and open it from the home screen | the new version both times; games, saves and settings as before | `PC5 start version=…` the new one |
| 98 | PC5 installed on a USB drive (homebrew folder), Update now | as on /data; the work folder `pc5-update` on that drive is gone afterwards | `PC5 update folder=/mnt/usb…` |

## Updating games, kept saves, movies and the touchpad (1.8.0)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 99 | Play a game and save; put a newer copy of its folder (another file changed) on the USB drive; △ › Update, choose it, Update | "Updating …" through copying, checking and putting the files in place, then "… is updated"; the game starts with its save; the changed file is the new one | `PC5 update id=… from=…`, `PC5 updated id=… files=…` |
| 99a | △ › Update, choose another game's folder | "… can't be updated: This folder doesn't have …" | `PC5 problem update …` |
| 99b | △ › Update, then ○ while it copies, Stop | "Update stopped"; the game plays as before | `PC5 update ended cancelled=1` |
| 100 | △ › Uninstall › Keep saves | "… is uninstalled", saves kept; Options › Game maintenance › Uninstall game lists "Saves of …" after the games | `PC5 uninstall id=… keep_saves=1` |
| 100a | Install the same game again | "Put back the saves of …?"; Put them back: the game starts with its save and its options | `PC5 install saves=…/saves/…` |
| 100b | Keep saves again; Options › Game maintenance › Uninstall game › Saves of … › Delete | "The saves of … are deleted"; the row is gone | `PC5 saves delete name=…` |
| 101 | A game with WebM or ASF movies | the movies play smoothly, in time with their sound | no `EBML` warnings from FFmpeg |
| 102 | A game whose Back opens a menu, Xbox controller mode: press the touchpad | the game's Back menu, and no share menu from the console | |
| 103 | Update PC5 from 1.8.0 to this version (Settings › Updates › Update now; from 1.8.2, Settings › System), then Restart PC5 | PC5 opens again by itself, the new version in About PC5, no CE-108255-1 | `PC5 restarting after its update eboot=/app0/eboot.bin` |
| 104 | △: the game menu; Graphics, Appearance, Manage; ○ in a group | five rows (six with Keyboard & mouse); each group's rows; ○ back to the first page on the group's row, △ to the library | |
| 105 | △ › Appearance › Cover; L1/R1; △, type another name, Done; ✕ | the cover it has first, then those found; the PS5 keyboard with the game's name; covers for the name typed; the library shows the new cover | `PC5 picture choices=…`, `PC5 screens keyboard open rc=0`, `PC5 picture saved kind=cover … failed=0` |
| 105a | △ › Appearance › Background; a Steam match; ✕; then Background › None | the background behind the library changes; with None, PC5's own backdrop | `PC5 picture saved kind=background … failed=0` |
| 106 | Fresh console (no `/data/prospero-win` yet): open PC5, then list `/data` over FTP | `prospero-win` is `drwxr-xr-x` (or more open), not `drwx------`; FTP can open it | |
| 107 | A Direct3D 12 game that needs feature level 12_0 (Unreal Engine 5) | no message from the game about DirectX 12 feature level 12_0; it goes on starting | |
| 108 | A game whose sound engine asks for spatial audio (7.1.4) | it has sound | `stream … opened: … 12 channels`, no `stream refused` |
| 109 | `prefixes/<game>/pc5/winedebug.txt` with `err+all,+loaddll,+process,+seh`; start the game | the log has the extra channels | `PW_WINE64 winedebug=err+all,+loaddll,+process,+seh` |
| 110 | Settings › System › Startup sound | the list has no "PC5"; cheap chinese speaker is the default | |

## The library's new look, System, picture sources, Java and batch games (1.8.2)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 111 | Open PC5 with a few games; move left and right | covers stand on a floor that mirrors them, the side ones turned a little towards the middle; the chosen one framed and lit in its cover's colours; its name and a pill with its bits, graphics and size under it; "N GAMES" at the bottom right; moving stays smooth | |
| 112 | Choose a game with a background, then one without | the background behind the covers, mirrored in the floor; for the other, a backdrop of its cover's colours | |
| 113 | Settings: the first page; System | Game maintenance, In games, Overlay, System, About; System has Startup sound, Startup sound volume (while a sound is chosen), Keep all logs, Picture sources, Check when PC5 opens, Check now | |
| 114 | System › Startup sound volume: 50 %, then 10 %; close PC5 and open it after each | the sound plays as the volume changes, quieter at 50 %, quiet at 10 %, and at that volume when PC5 opens; the console's Controller Speaker Volume doesn't change it | `PC5 settings … startup_volume=50`, `PC5 startup sound … volume=… percent=50 level=8192` (volume not negative) |
| 115 | System › Picture sources: Steam off; △ › Appearance › Cover on a game | no Steam cover among those found; the screens don't say where a picture comes from | `PC5 settings … pictures_off=0x1` |
| 116 | △ › Appearance › Cover on a game Steam's search misses but PCGamingWiki or Wikidata has | a cover found | `PC5 picture choices=…` |
| 117 | A Java game that carries its own runtime (`runtime/bin/server/jvm.dll`), made with jpackage | it shows as OpenGL; it starts in one program (no "process creation is not supported" for its own program) | `PC5 Java launcher: C:\Games\…\app in PATH`, `Loaded … jvm.dll` |
| 118 | A game that starts from a .bat file whose program is 64-bit, installed with an earlier PC5 as 32-bit | it starts as 64-bit | `zink provider=… architecture=pe64` |

## Scrolling, 32-bit FS, no 16-bit kernel, CPU sharing, firmware (1.8.3)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 119 | Open PC5 with 20 or more games, some with backgrounds; hold left, then right | the covers move smoothly, at the console's frame rate; a game's background shows a moment after it is chosen | |
| 120 | Fallout or Fallout 2 (falloutw.exe, fallout2.exe) | it gets past startup to its own screens | `wine-ps5: 32-bit FS base repaired (1): … for %fs:18, fs 0013 base …`: send the line |
| 121 | A game that looks up kernel32's UTRegister (Brian Lara International Cricket 2007), without a krnl386 override | it doesn't close at once; no `i386_set_ldt: Invalid argument` | `process_attach failed for forward 'krnl386.exe16.UTRegister'` |
| 122 | △ › Graphics › CPU sharing On on a game; start it | it plays; for a game that stopped for seconds at a time (Ori and the Blind Forest in Xbox mode), it no longer stops | `PW_WINE64 runtime thread_scheduling=1` |
| 123 | Open PC5 | the log names the firmware the console's Settings › System › System Software shows | `PC5 system firmware=13.60 …` (with `reported=…` when libkernel answers another): send the line |
| 124 | A 4:3 game that draws its own pointer from DirectInput's motion, Aspect ratio Keep, Keyboard and mouse with the pointer on; push the pointer right and down | the game's pointer reaches the right and bottom edges | |
| 125 | Open PC5 with a startup sound chosen, on a console whose own system is older (9.60, for one) | it opens and plays the sound | `PC5 startup sound … percent=100 level=32768`, then `played=… output=0`: send the lines |

A non-zero `rc=` on the light bar or triggers lines means the console refused that call; send
that log. On the startup sound line, a negative `port=` means the console refused the
controller's speaker.

## A launcher's game as itself, Game maintenance (1.8.6)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 126 | A game whose 32-bit launcher starts a 64-bit game (or a launcher and game that draw differently), installed on its launcher: start it | the launcher closes and the game starts, and plays as when it is chosen with △ › Manage › Change program | `PC5 hand-off: … starts the program its launcher started` |
| 127 | Options: the first page; Game maintenance | Game maintenance, In games, Overlay, System, About; Game maintenance has Install games from USB, Uninstall game, Game compatibility list; ○ from Install's and Uninstall's lists comes back to their rows there | |
| 128 | Game maintenance › Game compatibility list; scan the code with a phone | the phone opens the wiki's Game compatibility page; ○ comes back to the row | `PC5 compatibility list qr=…` (not 0) |

## Settings tools, a smaller display mode, XInput by WMI, a loopback network, crash logs (1.8.8)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 129 | Far Cry (FarCry.exe), which starts `Bin32\FarCryConfigurator.exe /Caller=FarCry` as it opens | it goes on to its own screens; it doesn't close and start the configurator | `wine-ps5: …FarCryConfigurator.exe is a helper: not handed off` |
| 130 | A game that follows the system's pointer, at a display mode smaller than the desktop (Sacred Underworld at 1024x768), Keyboard and mouse with the pointer on; push the pointer right and down | the game's pointer reaches the right and bottom edges; the picture is where it was | |
| 131 | Batman: Arkham Knight, Controller mode Xbox controller | the controller plays the game | |
| 132 | A Java game that lists the network interfaces (Shattered Pixel Dungeon: start a new game) | the new game starts | `wine: no nsiproxy (\\.\Nsi: error 2), NSI answers with the loopback interface alone` |
| 133 | An Unreal Engine 5 game on Direct3D 12 that makes a shared resource as it starts (STRETCHER.MEN) | it gets past its start, with no crash in `vkGetMemoryWin32HandleKHR` | |
| 134 | A game that crashes, then keeps logging (its own crash handler, or a game with a `winedebug.txt`) for a minute or more | its `issue-…zip` has `…-crash.log` with Wine's `Unhandled …` line in it | |
| 135 | Brian Lara International Cricket 2007: △ › Manage › Change program | ICC2007.exe first, HardwareSetup.exe after it | |
| 136 | After a game crashed: △ on PC5's message, scan the code with a phone | GitHub's form **A game doesn't run** opens, its title, Game, Program, PC5 version, Firmware and What PC5 saw filled in | `PC5 report log=… link=…` |

## Updates that download only what changed (1.9.0)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 137 | Update PC5 1.8.8 to 1.9.0 (Settings › System › Update now) | the download is the whole app's size (about 400 MB); 1.9.0 then has `pc5/manifest.txt` in its folder | `PC5 update using PPSA11018.zip size=…` |
| 138 | With 1.9.0 installed and the next version published: open PC5 | "PC5 x.y.z is available" gives the update zip's size (the release's `PPSA11018-update.zip`), not the whole app's; Later: Settings › System | `PC5 update state=4 latest=… zip=1` |
| 139 | Update now, let it finish, Restart PC5 | the download is about the update zip's size and quick; then "PC5 x.y.z is installed"; the new version in About PC5; games, saves and settings as before | `PC5 update version=… update_zip=…`, `PC5 update using PPSA11018-update.zip size=…`, `download rc=0 status=200 bytes=…` (the update zip's size), `PC5 update the update zip holds … of the release's … files; the rest are in place`, `placed=…`, `done` |
| 140 | Update now, then ○ while the update zip downloads | it stops; PC5 is as it was; it doesn't go on to the whole app | `PC5 update stopped: Cancelled.` |
| 141 | Put 1.9.0 back, change one of its files that the next version doesn't change (add a line to `LICENSE` over FTP), then Update now | the update zip is downloaded first, then the whole app; the update is installed and `LICENSE` is the release's again | `PC5 update can't use the update zip (1 files aren't in the update zip or on the console as it needs them); downloading the whole app`, then `PC5 update using PPSA11018.zip size=…` |
| 142 | Put 1.9.0 back and delete its `pc5/manifest.txt`, then Update now | only the whole app is downloaded; the dialog gave the whole app's size | `PC5 update: PC5's folder has no list of its files (pc5/manifest.txt), so the whole app is downloaded` |

## The thread data of 32-bit code the console didn't repair (1.9.1)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 143 | Fallout 1's falloutwHR.exe (the high-resolution patch's program): start it | it gets past its start to its intro or menu, with no crash in `ntdll.dll` reading FF81C098 | `wine-ps5: 32-bit FS base repaired (1)`, then if the console didn't take a repair, `the 32-bit FS base repair didn't take at …` once and `32-bit %fs: access done by Wine (1): …`; no `the 32-bit FS base was not repaired` |
| 144 | Fallout 1's falloutw.exe and Fallout 2's Fallout2.exe and HR.exe | they start as on 1.9.0 | as on 1.9.0 |
| 145 | Update from 1.9.0 to 1.9.1 with PC5's updater | the download is about the update zip's size, far less than the whole app | `PC5 update using PPSA11018-update.zip size=…` |

## Graphics API per game, a large overlay (1.9.2)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 146 | △ on a Direct3D 11 game › Graphics | Graphics API first: Auto, then WineD3D, WineD3D on Vulkan, Auto again with ✕ or right | `PC5 controls id=… graphics_api=WineD3D` |
| 147 | Start it with WineD3D | it starts and draws, slower than with DXVK | `dll_overrides=d3d8,d3d9,d3d10core,d3d11,dxgi,d3d12,d3d12core=b;opengl32=n`, `zink provider=ok`, wined3d's `GL_RENDERER "zink …"` |
| 148 | Start it with WineD3D on Vulkan | it starts and draws | `PW_WINE64 debug_env=WINE_D3D_CONFIG`, no `zink provider` line |
| 149 | A Direct3D 12 game: Graphics API › Wine's VKD3D, start it | it starts (or fails differently than with VKD3D-Proton) | `dll_overrides=…d3d12,d3d12core=b`, no `vkd3d-proton` lines |
| 150 | A DirectDraw game (Baldur's Gate II): Graphics API › WineD3D on Vulkan, start it | it draws; compare its pauses with Auto | `PW_WINE64 debug_env=WINE_D3D_CONFIG` |
| 151 | An OpenGL or Vulkan game › Graphics | no Graphics API row | |
| 152 | Settings › Overlay: every line on, Size › Large; start a game | two lines at a time in big digits, the next two every 3 seconds, the memory as "…G"; Normal again shows every line at once | `PC5 settings … size=large` |

## SteamGridDB, readable menus (1.9.3)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 153 | Open PC5 with no `/data/prospero-win/api_keys.txt` | it opens as before; the file is there afterwards, explained, `STEAMGRIDDB_API_KEY=` empty | |
| 154 | Settings › System › Picture sources | Steam, SteamGridDB (**No key**, its line naming api_keys.txt), GOG, PCGamingWiki, Wikidata; no Wikipedia | |
| 155 | Put a SteamGridDB key in api_keys.txt; △ on a game › Appearance › Cover | SteamGridDB's covers among those found, after Steam's | `PC5 picture choices=…`, no key anywhere in the log |
| 156 | The same with a wrong key | the other sources' covers as before; Picture sources' SteamGridDB row says it refused the key | `PC5 SteamGridDB refused the key in api_keys.txt (HTTP 401)`, once, with no key |
| 157 | Install a game Steam has (key in place) | its cover from Steam; its background from Steam, or SteamGridDB's where Steam has none | `PC5 background game=… from=Steam` or `from=SteamGridDB` |
| 158 | Uninstall a game with Keep saves | `/data/prospero-win/saves/<game>/pc5/` has no cover.bin, background.bin or background.txt; controls.txt is there | |
| 159 | A game installed with 1.9.2: open PC5 1.9.3 | its `pc5/cover-original.jpg` (or .png) is gone, its cover as before | |
| 160 | △ on a Direct3D 11 game › Graphics › Graphics API | a window: Auto, WineD3D, WineD3D on Vulkan, each with its whole description, the one in use marked "In use"; ✕ takes the one chosen, ○ leaves it | `PC5 screen=graphics-api` |
| 161 | Walk every row of every Settings page and a game's menu pages | no description cut with "…", none under the ✕ / ○ hints | |
| 162 | Settings › System › Check now, with a newer PC5 out | its rows fit above the hints | |

## Rumble, older games' Direct3D, folder listings (1.9.4)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 163 | Settings › In games › Vibration 100 %; a game with rumble in Xbox controller mode (a racing game, a shooter's recoil) | the controller's motors vibrate, not only the triggers | `PW_WINE64 rumble left=… right=… status=ok rc=0 mode_rc=0` |
| 164 | The same in DualSense mode, and a second player's controller | their motors vibrate | `PW_WINE64 player 2 rumble … mode_rc=0` |
| 165 | Vibration Off in PC5's Settings | no motor rumble | |
| 166 | A DirectDraw game PC5 draws through DXVK (Fallout 2 with its high-resolution patch) | its intro and title screen look right | no `Failed to get a GL context` |
| 167 | Splinter Cell: Conviction | its system check no longer lists "256 MB of video memory is required" or "Operating System not supported" ("No audio device detected" may stay) | `PC5 prefix Windows versions added` the first time |
| 168 | Heroes of Might and Magic: Olden Era: Start Game › a mode; Load Game | the mode starts; Load Game lists saves | no `can't list directory` line; Player.log has no `IOException` for `Story_maps` |
| 169 | Any DXVK game that ran on 1.9.3 | runs as before | `debug_env=WINE_D3D_CONFIG` in its profile line |

## Extra large overlay (1.9.5)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 170 | Settings › Overlay: FPS and CPU on, Size › Extra large; start a game drawn by PC5 (Direct3D 9 through DXVK) | every line at once (no taking turns), twice Normal's size, in its corner, not cut at the screen's edge | `PC5 overlay magnified=1 rc=0` |
| 171 | The same in a Vulkan or Direct3D 12 game | the same | `pw_pc5_hud: overlay magnified=1 rc=0` |
| 172 | Each corner (Settings › Overlay › Corner) | the overlay fully on screen in each | |
| 173 | Size back to Large, then Normal | its own size again | `magnified=0 rc=0` |

## Play from USB (1.9.5-usb-test, a test build)

| # | Do | Expect | Expect in the log |
| --- | --- | --- | --- |
| 174 | Install games from USB › a game › △ Play from USB | done in seconds, nothing copied; the library shows `USB ·` before its details | `PC5 install id=… usb=1` |
| 175 | Start it | it starts and plays as a copied one would | `PC5 play from usb dir=/mnt/usb…`; no `can't open` or `not found` for its files |
| 176 | Save in the game, quit, start again | the save is there | |
| 177 | A game that saves into its own folder | it saves (or the log shows the write refused on the drive) | |
| 178 | Unplug the drive, start it | "Plug in the USB drive with …", no start | `PC5 problem play …: Plug in the USB drive` |
| 179 | Plug the drive into another port, start it | it starts | `dir=/mnt/usb<other>` |
| 180 | Uninstall it (Keep saves, then Delete all) | the console's part gone; the game's folder on the drive unchanged | |
