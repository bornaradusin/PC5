# Per-game library music

The first version of library music, which played a WAV file where it was. For music
copied to the console, and MP3, see [library-music-mp3.md](library-music-mp3.md).

Select a game, press Triangle, open **Appearance**, then **Library music**.
Browse console storage or a USB drive and select a music file. **Remove library
music** clears the association without deleting the original file.

Supported files: RIFF/WAVE, PCM format 1, signed 16-bit little endian, 48 kHz,
one or two channels. MP3, AAC, compressed WAV and other sample rates are rejected
with an explanation. For example, an existing FFmpeg installation can convert:

```sh
ffmpeg -i input.mp3 -ar 48000 -ac 2 -c:a pcm_s16le output.wav
```

Music starts after the selected cover stays selected for 350 ms, loops, and
fades in/out over approximately 100 ms. It uses the console's main stereo
output, at a fixed 30% amplitude, separate from the controller startup sound.
The startup sound ends when the first music track starts. Music continues in
the selected game's menu and stops on other screens, including the file picker,
settings and launch preparation. Returning to the library selects it again.
Games with no association stay silent. The user keeps control of overall volume
through their audio device; this change adds no music volume setting.

The association is an absolute file path saved atomically in
`<root>/prefixes/<prefix>/pc5/music.txt`. **The track is referenced, not copied.**
Leave the source drive connected and the file in the same place. A missing file
is silent and recorded in the console log; select it again if its mount path
changes. No music is downloaded or bundled.

Implementation: `title/pc5_sound.[ch]` reads a bounded WAV stream;
`title/pc5_ui.[ch]` handles selection, persistence and the platform callback;
`title/pc5_console.c` owns the music worker, main output and lifecycle cleanup.
There is at most one music worker; it is joined before replacement, game launch
or application restart. WAV data is streamed instead of loading the whole song.

## Validation and build status

`python3 tests/test_music_stream.py` compiles the actual streaming functions and
the UI's music-follow function in a host harness. It checks stereo, mono,
looping, nonstandard chunk ordering/padding, invalid files, selection delay,
rapid browsing, menu continuity, launch/return, missing associations and an empty
library/filter. On Windows use a Visual Studio developer prompt and `--msvc`.
The harness isolates these functions; it does not compile the entire app.

The change was checked with MSVC C11, warnings treated as errors, and the
translation consistency checker. Full Linux/PS5 builds and hardware audio/UI
tests have **not** been performed. Follow the original README for dependencies
and `tools/build.sh`; follow `tests/MUSIC-CHECKLIST.md` on a console before using
or distributing a built application. This archive contains modified source,
not an installable PS5 application.
