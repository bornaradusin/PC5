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
| 5 | Options › Install games from USB, tick two folders with □, press Options | "Getting ready 1 of 2", then the list with covers | `PC5 folder=… queue=1/2`, `PC5 cover choices=… queue=…` |
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

A non-zero `rc=` on the light bar or triggers lines means the console refused that call; send
that log.
