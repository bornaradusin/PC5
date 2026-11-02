# Experimental background video (custom update 3)

**Triangle > Appearance > Background** now contains:

1. Background image (the existing picture chooser).
2. Background video, with **Choose video file** and **Remove background video**
   inside it. Choose video file opens the existing internal/USB picker, then
   validates and copies the selected file on a worker. Square on either the
   Background video entry or Choose video file opens phone reception by QR.

Remove background video asks for confirmation with Cancel selected first.
Confirming deletes the managed copy and returns to the image; the original
source file is kept. Back cancels and returns to the removal row. Back from
each submenu returns to its parent, and Triangle closes the game menu.

## First supported format

Use H.264/AVC video, preferably in MP4, up to **1920x1080 and 60 fps** (59.94 is
accepted). Lower resolutions/rates are accepted. The file must be no larger
than 1 GiB. Other codecs, higher resolutions and higher nominal rates are
rejected. Files are probed by content, not only their extension.

**1080p60 is an experimental target, not a measured performance guarantee.**
This is software decoding with four FFmpeg threads, using the project's pinned
FFmpeg 8.1.3 build. Frames are scaled to the 1920x1080 interface and shaded for
legibility. Late frames are dropped instead of queued indefinitely; the UI
consumption rate and dropped/decoded counts are logged every five seconds.
No hardware video decoding or frame generation is claimed.

Only the video stream is sent to the decoder. The player does not open any
audio device, so a file with audio still plays silently and the existing
per-game music can continue. The source file is copied unchanged, not
transcoded or stripped of its audio track. The decoder starts as soon as the
cover is selected, without an intentional waiting period; the still image stays
visible until the first decoded frame is ready. Playback loops while the cover or
its menu remains selected; it stops on other screens and before game launch.
If opening/decoding/looping fails, PC5 keeps the existing still-image backdrop.
The image remains available even while a video is assigned; remove the video
to show only the still image again.

## Storage

```
<root>/prefixes/<prefix>/pc5/background-video/clip-a.mp4
<root>/prefixes/<prefix>/pc5/background-video/clip-b.mp4
<root>/prefixes/<prefix>/pc5/background-video.txt
```

The normal root is `/data/prospero-win`. The inactive slot is prepared as
`.part`, checked for source size/change and closed, then renamed. Only after
success is the association committed atomically and the old slot removed.
Back cancels import and keeps the old association. Originals outside these
two application-owned slots are not deleted. Import validation decodes the
first frame; corruption later in a clip can still trigger the playback fallback.

Wait for import to finish before disconnecting USB. Subsequent playback uses
the copy in the game's directory, including after closing and reopening PC5.

## Build and tests

The native title links the same static FFmpeg libraries already used by Wine,
with custom AVIO callbacks because that build enables no protocols. It also
uses Wine's existing `pw_ffmpeg_libc.c` adapters and pthread stack wrapper.
Other host checks build a decoder-unavailable fallback without requiring
FFmpeg. `tests/test.sh` links the new video module and its picture navigation
tests enter the new Background submenu before opening the image chooser.

`python3 tests/test_video_ui.py` tests the actual menu layout/parent routing
and the decoder-unavailable fallback. It runs locally without FFmpeg.

`python3 tests/test_video.py` requires Linux FFmpeg development packages and
FFmpeg/libx264. It compiles the real player with address/undefined sanitizers,
generates synthetic H.264 1080p60 with audio and 120fps samples, then exercises
import, source removal, playback/loop, stop/restart, unsupported input and
cancellation. This test must pass in Ubuntu before the PS5 build is exported.
It does not measure PS5 performance or prove device compatibility.

Full title linking, all host checks and hardware playback remain required for
this update. `build-pc5-video.sh` applies the source update with a backup and
preserves existing build caches and prior working app ZIPs.

## Hardware checklist

- Import a short H.264 1080p60 MP4 from USB; ensure the UI remains responsive.
- Check no clip audio is heard, and independent library music continues.
- Disconnect USB, switch covers, return and restart PC5: local playback works.
- Switch rapidly, open settings, launch/return from a game: no residual worker.
- Replace/remove/cancel video; verify originals stay and old association survives
  failures. Confirm image choice/back navigation and music settings still work.
- In Background > Background video, confirm both file selection and Square phone
  reception work. Open Remove background video: Cancel is selected first; Back
  and Cancel keep the clip and focus the removal row. Only confirmation removes it.
- On selecting a cover, verify decoding starts without an intentional wait and
  the still image remains until the first frame is ready. Repeat rapid browsing.
- Try invalid input, >1080p and >60fps: useful error, previous video preserved.
- Read the diagnostic frame-rate log with different 1080p60 clips; check for
  dropped frames, UI lag and memory growth. Keep the working MP3-only build for
  rollback until hardware testing succeeds.

The nested menu, removal confirmation and immediate-start changes still need
on-device verification; earlier playback checks do not validate these changes.
