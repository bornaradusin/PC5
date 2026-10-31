PC5 has a page of its own for a browser, so you can manage it from a phone or a PC on the same network as the console: send a game to install, change a game's launch options, and download logs for a report. It works while PC5 is open.

## Opening it

1. In PC5, turn on **Settings › Network › Web page**. The row shows the address, for example `http://192.168.1.20:8125`.
2. Open that address in a browser on your phone or PC (Chrome, Safari, Firefox, Edge). On a phone, press **△** on the **Web page** row and scan the QR code (from 2.0.4).
3. If you set a username or password in **Settings › Network**, the browser asks for them.

The page stops when you leave PC5, and starts again the next time PC5 opens while **Web page** is on.

## What it shows

| Part | What it does |
| --- | --- |
| The top line | PC5's version, the free space on the console, and the free space where the INSTALL folder is, with links to the [game compatibility list](Game-compatibility), this wiki and PC5's own issue form. |
| Send a game | **Choose a folder** sends a game's installed folder to PC5's [INSTALL folder](Installing-games#the-install-folder), one file after another, with a progress bar. When it is done, PC5 asks to install the game, as after FTP. A phone that can't choose a folder can use **Choose files**: type the game's folder name first, and the files go into that folder. |
| Games | Every game in the library, with its settings (Graphics API, controller, frame generation, memory, CPU sharing) and a **USB drive** tag for one played from a drive; **Find a game** when there are many. **Newest log** downloads the game's newest log zip. **Report a problem** (from 2.0.4) downloads it too and opens GitHub's issue form with the game, PC5's version, what went wrong, the log's name and the settings filled in: attach the zip there. **Launch options** edits the game's [launch.txt](Launch-options), **Environment variables** its [env.txt](Launch-options#a-games-environment-variables-envtxt) and **Extra logging** its `winedebug.txt` (only when an issue asks for it; empty it afterwards, as it slows the game down): **Save** keeps them for the game's next start, and saving an empty box deletes the file. **Which game it is** shows the Steam app ID PC5 picks the game's pictures by, and where it came from; type another to fix a cover or background that is another game's (see [Which game it is](Covers#which-game-it-is)). **Refresh** reads them again. |
| Logs | Every log PC5 keeps, newest first, with its date, those that had a problem marked, to download and attach to an issue. See [Reporting a problem](Reporting-a-problem). |

## Good to know

- It uses port 8125. It is plain HTTP: with no username or password set, anyone on your network can open it, and even with them nothing is encrypted. Use it on your home network only.
- Sending a big game is quicker over a cable than over Wi-Fi, from a PC than from a phone, and quickest from a USB drive.
- A file is kept only once all of it has arrived: a transfer that stops part way leaves nothing behind. Send the game again to finish it.
- The page can't start or stop games, and it doesn't see anything on the console outside PC5's games, their launch options and its logs.
