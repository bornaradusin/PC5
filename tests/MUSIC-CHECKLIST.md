# Library music: hardware checks still required

## Nested menu and removal confirmation (pending on PS5)

- Triangle > Appearance shows Library music once; it opens Choose music file
  and Remove library music. Choose opens the existing console/USB picker.
- Square on Library music and on Choose music file opens the same phone QR.
  It is not offered on Remove library music. Import from both sources and check
  the return focus stays on Choose music file inside Library music.
- Remove library music opens a confirmation with Cancel selected first.
  Cancel and Back preserve the association and local track, and return focus
  to Remove library music. Confirm removal: playback stops, the managed slots
  and association are removed, and the original source file remains.
- Back from Library music focuses its entry in Appearance. Triangle from either
  level closes the game menu. Repeat after cancelling the file picker or QR.

## Custom update 2: MP3/offline import

- Build and pass both isolated music checks and the project host checks.
- Import CBR and VBR MP3, mono/stereo, 44.1/48 kHz and lower sample rates;
  test ID3 metadata and uppercase .MP3 filenames.
- Import a PCM16 WAV at 44.1 kHz and confirm it now works without conversion.
- During import, confirm the UI still animates, Back cancels, and the previous
  selection stays intact on cancel, invalid input or source-drive removal.
- Wait for successful import, disconnect USB, select another cover and return:
  the imported music plays. Close and reopen PC5 with USB disconnected: same.
- Import a replacement for one game: the new track plays and the inactive
  managed slot is removed. The original USB file stays unchanged.
- Choose music for a second game, disconnect USB and switch between both.
- Confirm Remove library music: imported slots and association are removed, original
  source stays unchanged, and no music plays on that cover.
- Re-select an old external association from update 1 once to import it.
- Verify launch stops playback and returning to PC5 resumes it.

## Original playback checks (update 1)

- Build on the project's documented Linux environment; run `tests/test.sh`.
- In Appearance choose different supported WAV files for two games. Check
  music comes through the TV/main audio output, with both stereo channels.
- Browse quickly: no overlapping songs; pause on a cover: its song starts.
- Leave a song selected longer than its length: it loops.
- Open the selected game's menu: music continues. Open settings or the file
  picker: music stops. Back/cancel returns to the correct game menu.
- In the picker ensure Triangle/Square do not expose destructive file actions.
- Select MP3, compressed WAV, a 44.1 kHz WAV or a damaged WAV: a useful error,
  no saved replacement, and Back still works.
- Remove the association: silence, the source file remains unchanged.
- Launch a game: music stops before the game starts. Return to PC5: the
  selected cover's music resumes. Repeat with a failed game launch.
- Restart PC5: the association remains. Remove the source USB: no crash;
  select the track again after reconnecting if the mount path changes.
- Try the empty library and an empty library filter: silence.
- Test application update/restart while music is playing; no remaining audio.
- Check startup sounds with and without a selected game's music association.
