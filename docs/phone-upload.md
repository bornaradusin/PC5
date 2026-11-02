# Receive appearance media from a phone (custom update 4)

The existing Choose action and internal/USB file selection are retained.
In Triangle > Appearance, highlight Cover, Tile picture or Library music;
or open Background and highlight Background image or Background video.
The footer offers **Square: Receive from phone** for these five appearance
options. It is also available on **Library music > Choose music file** and
**Background > Background video > Choose video file**, so both the submenu
entry and its file-selection row retain the shortcut. Cross on a Choose row
opens the existing internal/USB picker. Removal rows have no phone shortcut.
Triangle keeps its existing behavior: close the game menu and return to the library.

Connect the phone and PS5 to the same local network, or connect the PS5 to
the phone's hotspot if that phone allows access to its tethered devices.
Scan the QR code and choose one file in the phone browser. The TV shows upload
progress; the phone shows transfer progress and permits aborting the upload.
Keep the QR screen open. Back cancels reception and invalidates the link.

The QR code contains a temporary local HTTP capability URL, not an Internet
relay. Cellular Internet on a separate network cannot reach it by itself.
No account, cloud server or router port forwarding is needed. Some hotspots
or guest networks isolate devices; if so use a shared local network.

## Import and storage

- Cover/background image/tile picture: JPEG or PNG, up to 20 MiB. Decode with
  the existing size-limited image loader, compose to PC5's normal dimensions,
  and save using the existing picture/tile refresh path. HEIC is unsupported.
- Library music: MP3 or PCM16 WAV, up to 512 MiB upload. Run the existing
  background conversion/importer, retaining its 2 GiB output limit.
- Background video: H.264 in MP4/MOV, up to 1 GiB, 1920x1080 and nominal
  60 fps. Reuse the existing validation, local-copy and playback paths.
  MOV is a container: HEVC recordings are not accepted simply because .mov
  is allowed. Audio is ignored. Hardware-tested performance remains 1080p30.

The browser's Received response means upload finished; the TV gives the final
import/save result. Unsupported/corrupt media keeps the previous association.
Original files on the phone are never modified or deleted.

Uploads stage under `<root>/.phone/<random-token>.<extension>`; partial files
are removed on normal failure/cancellation, and completed staging files are
removed after import. Music/video end up in the same per-game managed slots
as USB imports. A power loss can leave a staging file in `.phone`; these are
temporary input copies, not the game's saved media.

## Network scope

Reuse port 8125 and PC5's existing QR generator and socket backend. Each
session gets a fresh 128-bit token from the existing PS5 kernel random source,
expires after ten minutes, binds to the selected game/type on the UI thread,
accepts at most one completed file and permits one upload at a time. No
uploaded filename supplies a filesystem destination. Closing invalidates it.

If the general web page was off, the receiver starts temporarily in phone-only
mode: other web routes are unavailable and it stops when reception ends.
If the page was already on, retain its current settings and authentication.
Phone URLs use the capability instead of HTTP Basic, for the narrow media
route only; the general page still requires its configured credentials.
Plain HTTP is intended for a trusted LAN/hotspot, not public exposure.

## Verification

`python3 tests/test_phone.py` runs the actual session/handler with bounded IO
and tests all five kinds, token rotation, one-shot upload, validation limits,
partial upload/cancellation cleanup, expiry and game-title HTML escaping.
`tests/test_web.py` adds real socket coverage, cancellation during reception,
and scope/authentication regression checks. `tests/test_video_ui.py` verifies
the five menu-row bindings and keeps existing menu/decoder tests.

Full Linux integration/build checks run before the helper exports a binary.
Phone browser behavior, hotspot routing, QR readability and final media
import/playback still require PS5 hardware tests for this update.
