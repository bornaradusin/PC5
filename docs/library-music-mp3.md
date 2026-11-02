# Library music: MP3 and offline storage

Select a game, press Triangle, then **Appearance > Library music > Choose music
file**. Choose MP3 or WAV from USB or console storage. Square on Library music
or Choose music file opens phone reception by QR. Wait for **Importing library music** to
finish; Back cancels it. The game then uses its own copy in console storage.
USB is required only during import, not during subsequent playback.

## Formats and storage size

MP3 uses the unmodified dr_mp3 v0.7.4 decoder (revision and license in
`title/third_party/dr_mp3.NOTICE`). Standard MPEG Layer III streams at 8, 11.025,
12, 16, 22.05, 24, 32, 44.1 and 48 kHz, mono/stereo, CBR and VBR are supported.
The decoder handles MP3 metadata such as ID3. File extension matching is case
insensitive. Damaged, encrypted, protected and non-MP3 data are not guaranteed
to decode; renaming another audio format to .mp3 does not convert it.

WAV import accepts RIFF PCM format 1, 16-bit mono/stereo at 8–192 kHz. Float,
24-bit, compressed and extensible WAV formats remain unsupported.

Both formats are decoded/resampled on a background worker into a 48 kHz stereo
PCM16 WAV using bounded buffers. This preserves the previously hardware-tested
playback path and avoids decoding MP3 while browsing. The local copy
takes about **11.5 MB per minute** (decimal), even when its MP3 source is much
smaller. The prepared file must remain below 2 GiB (about 3 hours at this rate).
Keep the source drive connected until import finishes.

## Storage and replacement

Each game has a private directory:

```
/data/prospero-win/prefixes/<prefix>/pc5/music/
```

More generally this is `<root>/prefixes/<prefix>/pc5/music/`. Import writes an
inactive `track-a.wav.part` or `track-b.wav.part`. After conversion succeeds and
the file closes, it is renamed to the inactive slot. Only then does the
UI atomically update `../music.txt` to point to it and remove the old managed
slot. Failure/cancellation keeps the previous association and track. A failed
metadata commit deletes the new inactive copy. An interrupted import may leave
a .part file which the next import overwrites; the previous track remains.

**Library music > Remove library music** asks for confirmation with Cancel
selected first. Confirming clears the association and removes both managed track
slots. Back or Cancel keeps the music and returns to the removal row. Back from
Library music returns to Appearance; Triangle closes the game menu.
Removal does not delete an external file originally selected. Game uninstall
removes this data with the game's prefix according to PC5's existing rules.

Associations to an external path, from before music was copied to the console, remain readable.
**Select those files again once to import them locally.** This update cannot
copy a source from a USB drive that is not connected.

Playback waits 350 ms on a stable cover, loops, and fades over about 100 ms at
the game's configured music volume (30% by default). It continues in the game's menu and stops on other
screens and before launching a game. No music is downloaded or bundled.

## Validation

- `python3 tests/test_music_stream.py`: actual streaming and selection functions.
- `python3 tests/test_music_import.py`: actual converter/commit code, synthetic
  FFmpeg/libmp3lame tones, 36 MP3 rate/channel/CBR/VBR cases, five WAV rates,
  source removal, invalid/missing input, cancellation, managed-slot cleanup and
  original preservation. The isolated worker adapter is synchronous; it does
  not test UI/thread scheduling.
- On Windows use a VS developer prompt and `--msvc`, and set FFMPEG if needed.
  On Linux the isolated tests use cc with address/undefined sanitizers.
- Repeat full app build, project host checks and PS5 tests for this update.
  Local isolated tests do not establish hardware compatibility.
- The nested menu and removal confirmation still require on-device checks;
  previous successful music playback does not validate this navigation.

The new `build-pc5-mp3.sh` helper applies the source update to the existing Linux
workspace, backs up changed source directories, and reuses `.build/`. It
exports the app only after build/checks succeed, with a new filename to preserve
the prior working binary. It does not install anything on PS5.
