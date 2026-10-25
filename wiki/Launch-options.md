Many PC games accept launch options: words added after the program's name that change how it starts, such as `-windowed` or `-nointro`. In PC5 you give a game its launch options in a text file called `launch.txt`, placed beside the game's `.exe`. PC5 reads it each time the game starts.

## Adding launch options

1. Find the folder that holds the game's `.exe` on the console:

   ```text
   /data/prospero-win/prefixes/<game>/drive_c/Games/<folder>/
   ```

   - `<game>` is the game's id: its folder name in lower case, with spaces and other signs turned into dashes (for example `neon-drift` for `Neon Drift`). A second game with the same name gets `-2` at the end, and so on. The easiest way to find it is to look in `/data/prospero-win/prefixes/` for the folder named like the game.
   - `<folder>` is the game's folder as it was on the USB drive (for example `Neon Drift`).
   - If the `.exe` is in a subfolder of the game's folder (for example `bin/`), go into that subfolder too: `launch.txt` goes **beside the `.exe`**.

   The game's menu (**△**) shows which program PC5 starts, under **Change program**.
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

## Changing or removing options

- To change them, edit the line and start the game again.
- To start the game without options, delete `launch.txt`, or put `#` in front of the line.

## Which options a game accepts

That is up to the game. Look for the game's PC launch options: the ones you would put in Steam's "Launch options", or in a shortcut's target on Windows.

Don't edit the game's profile in `/data/prospero-win/profiles/` instead: PC5 writes it again each time the game starts. `launch.txt` is the place for launch options.

See also [Files and folders](Files-and-folders) for where everything else is.
