When a game doesn't work, its log is how the cause is found. This page gathers what you may be asked to do on an issue: where the logs are, keeping every log, turning on a more detailed log for one game, and starting a game another way to see whether it changes anything. How to report a problem and which zip to attach is on [Reporting a problem](Reporting-a-problem).

## Where the logs are

```text
/data/prospero-win/logs/
```

- `issue-<when>_<game>.zip`: a session that went wrong (a crash, a game that closed before showing anything, or one that had to be closed).
- `log-<when>_<game>.zip`: each game's newest session, kept even when nothing went wrong. Playing the game again replaces it, so copy it first.
- Each zip holds PC5's log of the session, `about.txt` (what PC5 saw, the program, the console's firmware, and for a game made for Steam how it meets Steam: the Steam emulator, its app ID, Steam's DRM, other Steam stand-ins in the folder) and the game's own logs and crash files (`game/…`). You don't need to look for the game's own logs: PC5 collects them.
- Nothing is kept longer than seven days.

See [Finding the log](Reporting-a-problem#finding-the-log) for the details.

## Keeping every log

PC5 deletes its own sessions that went well. To keep every session, PC5's own included (an update or an install that went wrong, say), turn on **Settings › System › Keep all logs**, do what went wrong again, and attach the newest zips.

## A more detailed log, for one game

Wine, which runs the games, can write much more into the log. An issue's answer may ask for a line of Wine's log channels; this is where it goes:

1. On the console, open the game's folder of notes, `/data/prospero-win/prefixes/<game>/pc5/`. `<game>` is the game's id: its folder name in lower case, with spaces and other signs turned into dashes (look in `/data/prospero-win/prefixes/` for the folder named like the game).
2. Make a text file called `winedebug.txt` there, and write the line in it, for example:

   ```text
   err+all,+loaddll,+process,+seh
   ```

3. Start the game, do what goes wrong, and attach its newest zip as usual.
4. Delete `winedebug.txt` afterwards: a detailed log can make the game slower.

These are the lines most often asked for:

| To see | The line |
| --- | --- |
| Why a game crashes or closes | `err+all,+loaddll,+process,+seh` |
| Every call the game makes (the game is very slow with it: only when asked) | `err+all,+loaddll,+process,+relay` |
| The controller | `err+all,+loaddll,+process,+xinput,+hid,+setupapi,+dinput` |
| The sound | `err+all,+loaddll,+process,+mmdevapi,+xaudio2,+dsound` |
| Videos | `err+all,+loaddll,+process,+mfplat,+quartz,+wmvcore` |

The line takes the place of PC5's own (`err+all,+loaddll,+process`), so keep those three at its start. It can only have letters, digits and `_ + - , = .`, 127 characters at most; PC5 leaves out a line with anything else. Lines starting with `#` or `;` are notes, so you can keep other lines there for later:

```text
# err+all,+loaddll,+process,+relay
err+all,+loaddll,+process,+seh
```

## Starting a game another way

Some games can be told how to draw, which shows whether the problem is in one way of drawing. Give the option in the game's `launch.txt`, beside its `.exe` (see [Launch options](Launch-options)):

| The game is made with | You can tell it by | Options |
| --- | --- | --- |
| Unreal Engine | an `Engine` folder, and a program called `<Name>-Win64-Shipping.exe` | `-dx12` (Direct3D 12), `-dx11` (Direct3D 11), `-vulkan` |
| Unity | `UnityPlayer.dll` and a `<Name>_Data` folder beside the program | `-force-d3d12`, `-force-d3d11`, `-force-vulkan` |

A game can only draw the ways it was made with: one that wasn't made for the way asked for may say so and close. Say on the issue which option you tried, and attach the log of that session. Delete the line from `launch.txt` afterwards if it didn't help.

## Afterwards

Delete `winedebug.txt` and any line you added to `launch.txt` for a test, so the game starts as before. Turn **Keep all logs** off again if you don't need it: the logs still go after seven days either way.
