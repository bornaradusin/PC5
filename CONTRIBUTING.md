# Helping with PC5

Thanks for wanting to help! PC5 started as a hobby project, and it has grown into something
used by a lot of people every day. There are more games to look at and more issues coming in
than one person can keep up with, so every bit of help makes a real difference.

You don't have to be an expert, and you don't have to fix everything. One game made to work,
one bug tracked down, or one translation corrected is a real contribution.

## Ways to help

### Without writing code

- **Test games** and add each one to the
  [game compatibility list](https://github.com/bornaradusin/PC5/discussions/new?category=game-compatibility),
  whether it works or not.
- **Report problems well**: an [issue](https://github.com/bornaradusin/PC5/issues/new/choose)
  with the game's zip from `/data/prospero-win/logs/` attached and a short description of what
  you did and where it stopped. A log is what makes a problem fixable.
- **Check the translations** in your language: see
  [Translations](https://github.com/bornaradusin/PC5/wiki/Translations).
- **Help other players** in issues and discussions, for example by pointing them to the log
  they need to attach.

### With code

If you can code, or you're comfortable working with a good AI coding assistant such as
[Claude Code](https://claude.com/claude-code), you can take on an issue yourself. Many issues
already have a reply that explains what the log shows and where the game stops, which is a
good place to start.

1. **Fork** this repository and clone your fork.
2. **Build it.** On Debian 13 or Ubuntu 24.04 (x86-64), as root:

   ```sh
   apt-get install -y $(cat tools/packages.txt)
   tools/build.sh
   ```

   Or let GitHub build it for you: in your fork, **Actions › Build › Run workflow**. The app
   comes out as `PPSA11018.zip`.
3. **Pick an issue** and say in it that you're looking at it, so two people don't work on the
   same thing.
4. **Make the change** on a branch in your fork. Small, focused changes are the easiest to
   review: one game or one problem per pull request.
5. **Test it before you open a pull request:**
   - Run the host checks: `tests/test.sh` (and `tests/test.sh --shots` if you changed a
     screen, then look at the pictures in `.build/tests/shots/`).
   - **Try it on a PS5.** Copy your build's `PPSA11018` folder over your installed PC5 and
     check that the game or problem you worked on is fixed, and that a couple of other games
     you know still start as before. `tests/PS5-CHECKLIST.md` lists what to try.
   - Keep the zip from `/data/prospero-win/logs/` of your test: attach it to the pull request.
6. **Open a pull request** against `main`. Say what was wrong, what you changed, and how you
   tested it (which game, which console firmware, what you saw). Link the issue it fixes.

A pull request that hasn't been tried on a console can't be merged: what works on a PC's
Wine often behaves differently on the PS5.

## Where things are

- `title/`: PC5's own code, its screens, the library, installing and starting games.
- `patches/prospero-win/`: changes to prospero-win (the Wine runtime for the PS5), including
  Wine's own patches in `wine/patches/`.
- `patches/dxvk/`, `patches/vkd3d-proton/`, `patches/radv/`: changes to DXVK, VKD3D-Proton
  and the PS5's Vulkan driver.
- `lang/`: the translations (`tools/update-po.py` after changing any text on screen).
- `wiki/`: the wiki's pages.
- `tests/`: the host checks.

## Ground rules

- Be kind and patient with each other: most people here are doing this in their free time.
- No help with piracy, cracks or getting around DRM, and no anti-cheat workarounds.
- Code you send is under the project's licence (see `README.md`).

Thank you for helping make PC5 better for everyone!
