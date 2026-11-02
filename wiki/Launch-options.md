Many PC games accept launch options: words added after the program's name that change how it starts, such as `-windowed` or `-nointro`. In PC5 you give a game its launch options in a text file called `launch.txt`, placed beside the game's `.exe`. PC5 reads it each time the game starts.

## Adding launch options

1. Find the folder that holds the game's `.exe` on the console:

   ```text
   /data/prospero-win/prefixes/<game>/drive_c/Games/<folder>/
   ```

   - `<game>` is the game's id: its folder name in lower case, with spaces and other signs turned into dashes (for example `neon-drift` for `Neon Drift`). A second game with the same name gets `-2` at the end, and so on. The easiest way to find it is to look in `/data/prospero-win/prefixes/` for the folder named like the game.
   - `<folder>` is the game's folder as it was on the USB drive (for example `Neon Drift`).
   - If the `.exe` is in a subfolder of the game's folder (for example `bin/`), go into that subfolder too: `launch.txt` goes **beside the `.exe`**.

   The game's menu (**△**) shows which program PC5 starts, under **Manage › Change program**.
2. Make a text file called `launch.txt` there.
3. Write the options on one line, for example:

   ```text
   -windowed -nointro
   ```

4. Start the game. PC5 starts it with these options.

You can also put `launch.txt` beside the `.exe` on the USB drive before installing. It is copied with the game.

## How PC5 reads launch.txt

- PC5 uses the first line that isn't empty and doesn't start with `#` or `;`. Those lines are comments, so you can keep notes or other options in the file.
- It reads the file each time the game starts, so a change takes effect at the next start.
- Up to 500 characters of the line are used.

For example, this file starts the game with `-nointro`, and keeps another option for later:

```text
# Launch options for this game
; -windowed
-nointro
```

A game that starts from a `.bat` file gets the options in the batch file first, then the ones in `launch.txt` (placed beside the `.bat` file). See [Games that start from a .bat file](Installing-games#games-that-start-from-a-bat-file).

## Games PC5 sets up for you

From PC5 2.1.1, a few games get launch options from PC5 when they have no `launch.txt`: the ones their own launcher would give them, as they were tested on the console. A `launch.txt` replaces them.

| Game | Options PC5 gives it |
| --- | --- |
| Grand Theft Auto IV: The Complete Edition (`GTAIV.exe`) | `-availablevidmem 1536 -nomemrestrict -norestrictions -refreshrate 60 -fullscreen`, and `-width` and `-height` of the game's **Resolution** |

GTA IV starts from `GTAIV.exe`, not `PlayGTAIV.exe`, which only opens the Rockstar Games Launcher. Current Steam and Rockstar copies of the Complete Edition still check for that launcher as they start and stop with a message that it's needed, so they don't run in PC5, like every game that needs a store's launcher; these options are for a copy that starts without it. Without them the game limits its settings to the video memory it finds. PC5 also gives it a DXVK option that makes it play more smoothly (`d3d9.weakRenderTargetFlushHint`). Mods such as the Ultimate ASI Loader (`dinput8.dll` beside `GTAIV.exe`) load as they do on a PC.

## Changing or removing options

- To change them, edit the line and start the game again.
- To start the game without options, delete `launch.txt`, or put `#` in front of the line.

## Which options a game accepts

That is up to the game. Look for the game's PC launch options: the ones you would put in Steam's "Launch options", or in a shortcut's target on Windows.

Don't edit the game's profile in `/data/prospero-win/profiles/` instead: PC5 writes it again each time the game starts. `launch.txt` is the place for launch options.

## A game's environment variables (env.txt)

Some games read environment variables, the way a fix on PCGamingWiki or a game's own documentation may ask you to set one. In PC5 you set them for one game in a text file called `env.txt`, in the game's `pc5` folder:

```text
/data/prospero-win/prefixes/<game>/pc5/env.txt
```

`<game>` is the game's id, as above. Write one variable per line, as `NAME=value`, for example:

```text
# Turns off the game's crash monitor
MY_GAME_NO_MONITOR=1
```

- PC5 reads the file each time the game starts, so a change takes effect at the next start.
- Lines starting with `#` are comments. Up to 32 variables are used.
- A name has letters, digits and `_` only. Names PC5, prospero-win and Wine set themselves are left out: those starting with `WINE`, `PW_`, `LD_`, `PS5` or `PC5`, and `PATH`, `HOME` and `TMPDIR`. Two kinds are let through:
  - `WINE_PS5_SYNC_SPIN_US`: how many microseconds a game's thread keeps checking for its next job before it sleeps (100 unless set). `WINE_PS5_SYNC_SPIN_US=0` turns it off, for a game that runs worse with it; a higher value, up to 1000, may help a game that wakes its threads very often.
  - `WINE_PS5_CURSOR_IDLE_MS`: how many milliseconds the mouse pointer stays on screen after you last moved it or clicked when it hides. PC5 sets it from the game's **Mouse pointer** option in its menu (0, never hides, unless that says **Auto-hide**: 5000); set here, it takes another time.
  - `PW_NATIVE_PROFILE=1`, `PW_VK_RADV_PROFILE=1`, `PW_NATIVE_SLOW_SYSCALL_US` and `PW_VK_RADV_EVENT_US`: a per-frame profile that records where each frame's time goes, for finding what slows a game down. It makes the log much bigger (PC5 keeps 16 MB parts of it while it's on) and the game a little slower, so delete the lines when you're done.
- The game's log says which were set: `PC5 env.txt set=1 MY_GAME_NO_MONITOR`.

To stop using them, delete `env.txt`, or put `#` in front of a line.

## Texture packs (Direct3D 9 games)

From PC5 2.1.1, a Direct3D 9 game can show your own, sharper pictures in place of its textures, without changing the game. Two lines in `env.txt` do it:

- `DXVK_TEXTURE_DUMP=dump` saves each texture the game loads, once, as a `.tga` file named after its contents, in a `dump` folder beside the game's `.exe`. Play the parts you want to change, then take the line out: it makes the game slower and fills the folder fast. `DXVK_CONFIG=d3d9.textureDumpMaxSize = 512` with it saves only textures up to 512 pixels wide and high.
- `DXVK_TEXTURE_PACK=pack` shows `pack/<name>.png` (beside the game's `.exe`) in place of the texture of the same name: give an upscaled or edited copy of a dumped texture the dumped one's name, as a PNG, of any size.

Only 2D textures in the usual colour formats are swapped. Setting `DXVK_CONFIG` in `env.txt` replaces PC5's own DXVK settings for the game (its video memory, and GTA IV's option), so add those to the same line, separated by `;`, if you still want them.

See also [Files and folders](Files-and-folders) for where everything else is.
