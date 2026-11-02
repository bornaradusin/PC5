# Media + Compatibility 1 (custom PC5 2.1.1)

This is an unofficial extension of PC5 2.1.1. Existing internal/USB pickers,
per-game phone reception on Square, and Triangle closing the game menu remain.
Game files, saves, upstream Wine/DXVK fixes and runtime version stay as in 2.1.1.

## Community compatibility

The top-level Compatibility tab, between Library and Install, displays the official
[community wiki](https://github.com/bornaradusin/PC5/wiki/Game-compatibility).
The reader fetches its raw Markdown over the existing certificate-checked HTTPS
client, on opening and every 60 seconds while the list is visible. Triangle
refreshes, Square cycles all seven source statuses, L2 searches, R2 clears the
search, Cross opens details, Up/Down scroll details, and Cross there shows the
original report's QR. Circle leaves details and QR screens; the root tab has no Back action (the console's accept/back setting applies). L1/R1 change tabs. Search and Clear
search also work from details. A keyboard-open failure now displays an error
instead of silently ignoring the search button; its result is logged for on-device diagnosis.
Clear search removes only the title query and retains the selected status filter.

The table includes title, status, tested PC5 version, store/build, controller,
helpful settings, notes, reviewer/date and report link. It is community testimony,
not a guarantee or an independent verification. No FPS or settings are invented.
GitHub's generation and raw-content caching can delay publication; this is
periodic checking, not a server-push subscription. No GitHub account/token is used.

Only a fully parsed response replaces the list. The last good raw table is saved
as `/data/prospero-win/compatibility.md`. Network errors or a changed source format
preserve it. The UI shows its last check time and whether it is the saved list.
The source is bounded to 4 MiB and 2,048 rows, with capped display fields.
Only issue/discussion report URLs within the original repository become QR links.

## Per-game media

Triangle > Appearance > Library music contains Choose music file and Remove
library music. Background > Background video similarly contains Choose video
file and Remove background video. File selection retains internal/USB browsing;
Square on either media submenu entry or its Choose row opens phone reception.
Removal asks for confirmation with Cancel selected first and keeps the original
source file. Back/Cancel returns to the removal row without changing the media;
Back from a submenu returns to its parent. Triangle still closes the game menu.

Triangle > Appearance > Media settings and backup:

- Music volume: 0–100%, default 30%. Music starts at an offset in seconds, loops
  from that offset, and retains its 100 ms entrance/exit fade. An offset beyond
  the WAV falls back to its beginning. MP3 still imports to local 48 kHz WAV.
- Video begins opening as soon as its cover is selected, with no intentional
  wait. The first frame appears when loading/decoding finishes. It is silent and
  independent of music. Start/end select a looping section in seconds;
  zero end means EOF. A start beyond the reported duration uses the beginning.
- Stretch, Fit (letterbox/pillarbox), Fill (center crop), and 0–80% additional
  dimming. The existing edge shading for text readability is retained.

Settings are atomically saved in each game's `pc5/appearance.txt`. The original
music/video association formats and a/b managed slots remain supported.

## Appearance backup

Export opens a folder chooser for internal storage or USB. Open the destination
folder and press OPTIONS (Save here). A unique game/time/suffix `.pc5look` name
is chosen without replacing an existing backup. The success message shows the
full saved path. Back cancels folder selection. Restore
chooses a `.pc5look` from internal storage or USB and asks before replacement.
The original backup/source files are never removed by restore.

The versioned streaming container includes only cover/background/tile images,
managed music/video, media settings, and the background-none marker. No saves,
game executables, launch settings, credentials or logs are included. It has fixed
numbered slots, bounded sizes, and SHA-256 on each nonempty payload. Restores
validate the images, WAV, H.264 first frame and options before changing anything.
They stage locally, journal replacements, and roll back failures/interrupted
commits at the next library load. Payloads are flushed and synchronized before
commit; directory metadata durability after sudden power loss remains dependent
on the console filesystem. A malformed journal is retained with its recovery
files rather than being treated as an instruction. Free space is needed for both the old and new
media (and temporary decoder validation copy). An invalid or unavailable legacy
external music association fails export rather than silently omitting it.

## One phone QR for the library

Settings > Network > Library appearance from phone creates a ten-minute
temporary capability. Choose a game, one of the five appearance options, and a
file in the mobile page. A completed import allows another file using the same
QR. The page reports validation success/failure and shows upload progress/cancel.

The phone and PS5 must share a network or hotspot. This adds no Internet relay.
Successful upload chunks extend the library session's idle deadline; an active
large transfer is not limited to ten minutes. The original single-file QR keeps
its existing ten-minute lifetime.
If the existing web server was off, the temporary server serves only capability
routes and stops on leaving; if it was on, its original state and Basic login
remain. The capability exposes only installed-game names/IDs and the appearance
upload, not game launch, arbitrary files, logs or launch options. One upload is
allowed at a time and its game/type stays fixed until UI validation finishes.

## Validation

Windows checks: strict C11 syntax for the UI/server/new modules, real FFmpeg 8.1
H.264 1080p60 import/play/loop/stop, trimmed colour-sequence loop/fit/fill/dimming,
MP3 import matrix, WAV streaming/offsets, actual console mixer loop at full-scale
0/30/65/100% volume, compatibility parser/cache/network-failure tests, backup
roundtrip including music/video, corruption rejection and journal recovery,
legacy and library capability handlers, and browser JavaScript syntax. Native
draw routines were rendered with the app's actual fonts and visually reviewed.

The Ubuntu helper additionally runs Linux ASan/UBSan media/backup tests, real
socket server tests and the full upstream regression suite before exporting any
PS5 package. Windows host tests do not prove console frame rate or operation;
SDK build and on-device verification are separate required steps.
The revised nested media menus, removal confirmations and immediate video start
still need a fresh build and on-device checks.


The top-level Share your experience button (OPTIONS) shows a QR for the official
Game compatibility discussion form. Users submit their own report on GitHub;
PC5 does not post it automatically. Read user report opens the existing reviewer
report. The game menu's existing report action is preserved.


Rev3 adds Left/Right page navigation by eight filtered results, clamped at either
end and inactive in details. The footer now repeats the share shortcut using
the three-line Options symbol and the label Share; the same symbol/label appears
in the header. User-supplied startup sounds Play Has No Limits (1.656s) and
Welcome Arabic (1.292s) are included as additional named choices. They were
converted to 48kHz PCM16 mono; existing sounds and the default are retained.

Home focus now uses Down to enter and Up to leave. The Focus mode footer text
is removed. Focused Home shows the Up arrow beside Back. Library grid direction
handling is unchanged. Rev4 includes the rev3 paging, Share symbol and both sounds.

Rev5 restores the full Share your experience header label beside the Options
symbol, centered using measured icon and text width. The footer remains Share.
