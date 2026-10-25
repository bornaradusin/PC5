# PC5

<p align="center"><img src="art/icon0.png" width="200" alt="PC5's icon"></p>

A launcher for your own PC games on a PS5 that runs homebrew. PC5 shows your games as a row
of covers, installs games from a USB drive in one copy each, and uninstalls them with their
saves. It runs the games with [prospero-win](https://github.com/mpereiraesaa/prospero-win)
(Wine on the PS5). The app is `PPSA11018`, its tile is called **PC5**. This is version 1.5.0.

No game is part of PC5, and PC5 downloads none: you install the games you own.

<p align="right">
  <a href="https://www.buymeacoffee.com/bornaradusin">
    <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png"
         alt="Buy Me a Coffee"
         width="140">
  </a>
</p>

## Using it

1. Copy the `PPSA11018` folder to the console the way you add any homebrew app, and start
   **PC5** from Home. The first time, the library is empty.
2. On a USB drive formatted as **exFAT**, put each game's installed folder (the one with its
   `.exe` in it) at the top of the drive, or in a `Games` folder there. The folder's name
   becomes the game's name in PC5.
3. In PC5 press **Options** › **Install games from USB**. Press **✕** on a folder to
   install that one, after its review screen (the cover PC5 found, the program it will
   start). Or tick several with **□** and press **Options**: PC5 reads them all, finds
   their covers, shows them in a list (✕ reviews one), and installs them one after
   another.
4. Choose the game in the library and press **✕** to play. To leave a game, hold the
   **touchpad** and **Options** for two seconds: the game is asked to close (it can save),
   and if it hasn't after five seconds it is ended. You are back in PC5.

Each game has a menu (**△**): Play, Controller, Frame generation, Resolution, Memory (32-bit games),
Change program (when PC5 started the wrong `.exe`), Home screen tile and Uninstall. Uninstalling deletes the game and everything it made: its saves, its settings,
its shader cache, its cover, its home screen tile's folder. There is no backup and no undo. The USB drive is never
written to.

| Buttons | |
| --- | --- |
| ✕ | play, choose (○ if the console is set to confirm with ○) |
| ○ | back |
| △ | the game's menu |
| Left / right, left stick | move between games |
| L1 / R1 | jump to the previous or next letter |
| Options | Settings |
| On the review screen | L1 / R1 other covers, □ another program |
| In a game | touchpad + Options for two seconds: back to PC5 |
| On a game's Windows pop-up | ✕ presses its highlighted button (OK, Yes), the D-pad moves between its buttons |

A USB keyboard works too: arrows, Enter, Escape, F1 for Settings, F2 for □, F3 for △,
Page Up and Page Down.

## Controller

Each game is played one of three ways, chosen in its menu (**△** › Controller) and kept for
that game:

| Controller | The game sees |
| --- | --- |
| Xbox controller | an Xbox controller (XInput), which most games from the last twenty years look for. The default |
| DualSense | a DualSense plugged in by USB, as on a PC: for games that know the DualSense or look for controllers another way (DirectInput, raw input, SDL). They get the touchpad and the motion sensors too, and their own rumble, light bar and adaptive trigger effects reach the controller |
| Keyboard & mouse | a keyboard and a mouse, for games without controller support. **Button mapping** sets what each button and each stick direction sends (a key, a mouse button or nothing), which stick moves the pointer and how fast; Reset to default gives W A S D on the left stick and the pointer on the right |

A change takes effect the next time the game starts. Holding the touchpad and Options for two
seconds leaves the game whichever is chosen.

A pop-up a game shows in a Windows box (a warning before it starts, a Yes / No question) is
answered with the controller in every mode: with the Xbox controller or the DualSense, ✕
presses the highlighted button and the D-pad moves between the buttons, and they reach
the game as keys only while such a box is in front. A USB keyboard and mouse work in
every mode too.

## Frame generation

Each game can be shown at 60 fps from 30 (**△** › Frame generation, kept for that game; off
by default). The game is held at 30 fps, and before each of its frames PC5 shows one made
between it and the one before, with [ps5-framegen](https://github.com/Swordpdf/ps5-framegen)'s
interpolator (AMD FSR3's optical flow). It suits games that keep 30 fps or a little more:
when a game falls below 30 its frames are shown as they come, without frames in between,
until it keeps up again. Frames made in between can show smearing around fast movement and
the screen's edges, and the game reacts a little later than without it. The overlay's FPS
counts every frame shown, the made ones too. It applies to games drawn with Direct3D 8 to
12, Vulkan or OpenGL, not to DirectDraw or plain Windows drawing, nor to 10-bit (HDR)
output. A change takes effect the next time the game starts.

## Memory

Games are told the console has 16 GB of memory, and how much of it is free, as a PC tells
them: a game that checks for enough memory before it starts finds it (the console's own
figures said about half a gigabyte).

A 32-bit game gets 4 GB of address space, what Windows gives one built to ask for it, and
what Proton gives every 32-bit game: room for big textures and mods. A game that misbehaves
with it can be set back to 2 GB (**△** › Memory, kept for that game). A 64-bit game's room
grows as it needs it, up to near the top of what the console gives a program.

## Home screen tiles

A game can have its own tile on the PS5's home screen (**△** › Home screen tile). Choosing
the tile starts PC5 straight into the game, and leaving the game goes back to the home
screen. The tile's picture is the game's cover.

PC5 writes the tile in `/data/homebrew` in the
[PS5 Forwarder Format](https://github.com/heydemoura/ps5-forwarder-format), so it needs:

- **ShadowMountPlus** (or another homebrew mounter that registers the apps in
  `/data/homebrew`): the tile appears on the home screen once it has registered it;
- **elfldr**, the payload loader (port 9021): the tile's program uses it to start the
  format's launcher, which then starts PC5.

Turning the row off, or uninstalling the game, deletes the tile's folder; if the tile is
still on the home screen after that, delete it there.

## Covers

Covers come from Steam's store, then GOG's catalogue, then Wikipedia (see below). Without
internet, or for a game none of them has, your own picture can be the cover, and it is
used for the game's home screen tile too:

- **When installing**: a `cover.jpg`, `cover.jpeg` or `cover.png` at the top of the game's
  folder on the USB drive is the first cover offered.
- **For an installed game**: a picture in `/data/prospero-win/covers/` named after the
  game's folder, its name in PC5 or its id (`Neon Drift.png`, say; `.jpg`, `.jpeg` or
  `.png`) becomes its cover the next time PC5 opens, and again whenever you replace it.

Pictures are cut to the cover's 2:3 shape.

## Settings

| | |
| --- | --- |
| Install games from USB | one game, or several in a row |
| Uninstall game | |
| Overlay › Show FPS, Frame time, CPU usage, CPU temperature, SoC temperature, Memory | an overlay over every game, a line each, updated every second: frames shown, their average time (ms), the game's share of the console's CPU, the CPU's and the console chip's temperatures (°C), and the memory the game uses of what it has (GB). FPS alone is the large counter; a line the console can't read is left out |
| Overlay › Corner | where the overlay sits |
| In games › Vibration | how strongly the controller rumbles with the game: off, 25, 50, 75 or 100 % |
| In games › Light bar | the main colour of the game's cover, the colour of what is on screen, or the console's own |
| In games › Adaptive triggers | off; rumble (L2 and R2 vibrate with the game's rumble); resistance (they push back as you press them); or rumble + resistance (they push back, and vibrate while the game rumbles: a trigger runs one effect at a time) |
| In games › Trigger rumble, Trigger resistance | how strongly the triggers vibrate, how firmly they push back: 25, 50, 75 or 100 %; each shows while its effect is on |
| Updates › Check when PC5 opens, Check now | asks GitHub for a newer PC5 (at most once an hour); a newer one is told with the console's notification, once |
| Keep all logs | off: only the logs of sessions that went wrong are kept |
| About PC5 | version, free space |

The light bar and the adaptive triggers follow these settings in every game. With the
DualSense controller, a game that sets its own light bar or trigger effects gets them
instead, from the moment it does.

## What runs

Every game is handled the same way: PC5 reads the game's program to see whether it is 32-
or 64-bit and what it draws with, and writes the same kind of prospero-win profile for it.

| The game uses | Through |
| --- | --- |
| Direct3D 8, 9, 10, 11 | DXVK 2.6.2-prospero2, with PC5's patch |
| Direct3D 12 | VKD3D-Proton 3.0.1 |
| Vulkan | RADV directly |
| OpenGL | Mesa Zink |
| DirectDraw / plain Windows drawing | Wine itself |

Only a game whose program draws with Direct3D 12 gets VKD3D-Proton; every other game gets
Wine's own Direct3D 12, as prospero-win's tested profiles have it (a library that only tries
Direct3D 12 stops there instead of going further and crashing).

PC5's DXVK lets a game share a picture between Direct3D 9 and Direct3D 11 (some games play
their movies that way) within the game, as the console can't share it through Windows'
handles. A game installed with an older PC5 gets this DXVK the next time it starts.

Every game runs with OpenSSL's SHA code turned off (`OPENSSL_ia32cap`), as Proton does:
games that carry OpenSSL 1.0.2 up to 1.0.2k, Unreal Engine 4.13 to 4.21 among them, crash
with it on the console's CPU.

What won't run: games that need Steam, Epic, EA or Ubisoft's launcher running, online DRM
or anti-cheat; programs that aren't
Windows programs. Whether a game runs is only known by trying it: PC5 makes no promise
about any game. When a game closes before showing anything, or crashes, PC5 says so the
next time it opens, and keeps the log. A crash's log also says where it happened: the
faulting thread's registers and the code addresses on its stack, as module+offset
(`speed.exe+0x1a2b3c`), which shows the game's code that led to it.

**.NET Framework**: .NET games and tools (XNA through FNA, MonoGame, WinForms) run with
[Wine Mono](https://gitlab.winehq.org/mono/wine-mono), the open-source stand-in for
Microsoft's .NET Framework that Wine itself uses. The first time a .NET game starts, PC5
copies its runtime (some 230 MB, once for every game) to `/data/prospero-win/pc5/`. It isn't
Microsoft's .NET: programs mixing .NET and C++ (C++/CLI) don't run, and some WPF doesn't.

**Fonts**: games that write text with Windows' fonts find Arial, Times New Roman and Courier
New (Liberation, made to take the same space) and Windows' Chinese, Japanese and Korean
fonts (Droid Sans Fallback), so their text has the size it was laid out for and those
languages show instead of empty boxes. Every game has them, including those installed with
an older PC5.

**Resolution**: a game drawn with Direct3D, Vulkan or OpenGL can be drawn at 1080p, 1440p
or 4K (**△** › Resolution, kept for that game). The console shows 1080p and 4K pictures as
they are, and a 1440p one scaled up to 4K. A higher resolution is slower: 4K is four times
the work of 1080p.

**The game's own DLLs**: a game that carries Microsoft's Visual C++ runtimes, Direct3D
compilers or D3DX helpers beside its program uses its own copies first, as Proton does for
the runtimes; Wine's own stand in for those it doesn't carry.

**Launch options**: a `launch.txt` beside the game's `.exe` holds its options (its first
line that isn't a `#` comment). PC5 reads it each time the game starts.

**Videos and sound**: games' videos and compressed sound (Media Foundation, DirectShow,
Windows Media and DMOs) are decoded with FFmpeg: H.264, WMV and VC-1, MPEG-1 and 2, AAC, WMA,
MP3 and others. A game installed with an older PC5 gets them the next time it starts.

**Language**: each game's Windows language is the PS5's, so a game that follows the Windows
language follows the console.

**Covers** come from Steam's store, then GOG's catalogue, then Wikipedia: public sources,
no account and no key. The review screen shows what was found (L1 / R1 for the others). A
game none of them has, without your own picture (see Covers), gets a cover made from its
own icon and name. A cover fills its frame
(cut to shape), and the downloaded picture is kept with the game. They are fetched over
HTTPS with Mbed TLS, checked against the console's root certificates and Mozilla's (which
the app carries).

## Where things are

PC5 keeps everything where prospero-win keeps it, in `/data/prospero-win`:

```text
/data/prospero-win/
  profiles/<game>.profile               what prospero-win starts (written by PC5)
  prefixes/<game>/                      the game's own Windows: registry, saves, settings
  prefixes/<game>/drive_c/Games/<folder>/   the one copy of the game
  prefixes/<game>/pc5/                  PC5's notes on the game, its cover and its picture
  covers/<game>.jpg / .png              your own covers (see Covers)
  prefixes/<game>/pc5-cache/radv/       its shader cache
  pc5/settings.txt                      PC5's settings
  pc5/last.txt, pc5/update.txt          the game played last; the last update check
  logs/issue-<when>_<game>.log          the log of a session that went wrong (kept 7 days)

/data/homebrew/<PPSA992xx–PPSA998xx>/   a game's home screen tile (see Home screen tiles)
```

A session's log is written while it runs (`logs/session-N.log`); when PC5 next opens, a
session that went well has its log deleted, and one that went wrong keeps it as
`issue-…`. Nothing in `logs/` is kept longer than seven days. A long session's log is in
parts: how it began (`-start`), the part before the newest (`-earlier`) and the newest. A
line that repeats is saved once, then counted (`the line before came N more times`).

Problems go to [GitHub's issues](https://github.com/bornaradusin/PC5/issues) with the log
attached, not to ProsperoStore's comments: the log is what shows the cause.

Games already in `/data/prospero-win` (installed some other way) show up in PC5 too, and get
a cover the first time PC5 opens. An install or uninstall cut short (the console switched
off) is finished, by deleting, the next time PC5 starts.

## Building

On **Debian 13 or Ubuntu 24.04** (x86-64), as root:

```sh
apt-get install -y $(cat tools/packages.txt)
tools/build.sh
```

The app is `release/PPSA11018/` and `release/PPSA11018.zip`, with the source of everything in
it as `release/PPSA11018-sources.tar.xz` and Wine Mono's own source archive beside it
(`release/wine-mono-11.3.0-src.tar.xz`); or all of these in the folder `PC5_RELEASE` names
(`PC5_RELEASE=/mnt/media/builder/releases/PC5 tools/build.sh`). Everything else the build
makes stays in the repo's `.build/`. Each step prints one line and writes
`.build/logs/<step>.log`; run one step again with `tools/build.sh <step>`. Elsewhere the
script runs itself in Docker (`tools/Dockerfile`). A first build takes a few hours, most of
it Wine; Zink's link wants about 8 GB of memory per job (`PC5_ZINK_JOBS`, default 2).

`tests/test.sh` runs the host checks (after `tools/build.sh sources`); `tests/test.sh
--shots` also draws every screen into `.build/tests/shots/` with made-up games.
`tests/PS5-CHECKLIST.md` is what to try on a console.

**Releasing**: the update check reads the newest release of
<https://github.com/bornaradusin/PC5>, by its tag (`v1.0.0`, `v1.1.0`, …). Publish each
version as a GitHub release with that tag, with `PPSA11018.zip`,
`PPSA11018-sources.tar.xz` and `wine-mono-11.3.0-src.tar.xz` attached, and raise `PC5_VERSION` in `title/pc5.h` and
`contentVersion` in `sce_sys/param.json` with it.

## How it is made

- `title/`: PC5's own code, plain C built into prospero-win's title: the screens
  (`pc5_ui.c`, drawn on the CPU by `pc5_draw.c`), the library, installing and uninstalling
  (`pc5_library.c`), home screen tiles (`pc5_home.c`, with the PS5 Forwarder Format's
  psfwd in `third_party/psfwd`), USB folders (`pc5_source.c`), reading Windows programs (`pc5_pe.c`),
  covers (`pc5_cover.c`, `pc5_image.c`, `pc5_json.c`), HTTPS on Mbed TLS (`pc5_net.c`),
  settings, updates and logs (`pc5_settings.c`, `pc5_update.c`, `pc5_logs.c`), the game's
  side (`pc5_game.c`, the loading screen `pc5_matrix.c`) and the console's
  (`pc5_console.c`, `pc5_net_ps5.c`, `pc5_game_ps5.c`).
- `patches/prospero-win/`: patches to prospero-win v0.2.0, so its title shows PC5
  instead of its launcher, holds 128 games, gives each game the PS5's language and its own
  shader cache, and applies PC5's settings in games (with libvulkan's overlay and screen
  colour); the sticks' directions as keys; the DualSense a game finds as on a PC (Wine's
  xinput makes it in the game's process); Windows.Gaming.Input finding the Xbox
  controller; the computer's name without wineboot; and Wine's XAudio2 keeping an effect
  alive while a voice holds it, and ignoring calls on a voice the game destroyed; games'
  videos and sound on FFmpeg (Wine's winegstreamer decoders on FFmpeg in place of
  GStreamer, and winedmo); Frame generation in RADV's `libvulkan.prx`; a game started from
  a home screen tile, and back to the home screen after it; the controller answering a
  game's Windows pop-up (Wine's PS5 driver sends ✕ and the D-pad only to a dialog box);
  a crash report in the log, as the console runs no debugger; and the drive letters
  without a mount manager (Wine's GetLogicalDrives), so a game sees C:.
- `patches/dxvk/`: PC5's patch to DXVK 2.6.2-prospero2: textures shared between a game's
  Direct3D 9 and Direct3D 11 within the game, where memory can't be shared through handles.
- `tools/`: the build, the Windows template (`make-prefix.py`), the font atlas
  (`make-font.py`) and the app (`make-app.py`). `art/make-art.py` draws the icon and the
  Home background.

## Credits and licence

PC5's own code is free software under the GNU LGPL 2.1 or later (`LICENSE`), like
prospero-win. The app as built holds the PS5 boilerplate's startup code and `libc.prx`
(GPL 3.0 or later), so the app as a whole is under the GPL 3.0 or later, as prospero-win's
own release is; its source is `PPSA11018-sources.tar.xz`, with Wine Mono's in
`wine-mono-11.3.0-src.tar.xz`.

It is built on prospero-win by mpereiraesaa and the projects it uses: Wine, DXVK, Mesa
(RADV, Zink; including mihawk-99's PS5 work), the PS5 payload SDK and boilerplate, and the
Lapy helper. Also Wine patches from PS5_MihawkOS by mihawk-99 (LGPL 2.1 or later: thread
alerts without a descriptor, mains power, a missing program, dnsapi, dxcore; the console's
free memory), a logging patch from NFS Black Box by lavavex (LGPL 2.1 or later), Wine Mono
(MIT, its parts under their own licences, `LICENSES/wine-mono/COPYING`), FFmpeg (LGPL 2.1 or later), ps5-framegen by Swordpdf (GPL 3.0 or later,
its shaders from AMD's FSR3 under MIT), VKD3D-Proton (LGPL 2.1 or later), Mbed TLS (Apache 2.0 or GPL 2.0 or
later), Mozilla's root certificates (MPL 2.0), stb_image and stb_image_write (public domain /
MIT), the PS5 Forwarder Format by heydemoura (its tile program and psfwd, GPL 3.0 or later),
the Liberation fonts (SIL OFL 1.1, `LICENSES/liberation-fonts/LICENSE`), Droid Sans Fallback
(Apache 2.0, `LICENSES/droid-sans-fallback/NOTICE`) and the Inter font (SIL OFL 1.1,
`fonts/OFL.txt`). Blur5 and sahin were inspiration only; no
code was taken from them. See `NOTICE.md`.

PC5 is not made by or connected to Sony, Valve, GOG, GitHub or the Wikimedia Foundation.
