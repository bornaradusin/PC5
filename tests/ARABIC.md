# Arabic localization

Arabic remains logical UTF-8 in `lang/ar.po`. Do not reverse its words or store
presentation forms in translations. Unicode FSI/PDI around substituted string
values keeps game names and paths independent of the surrounding sentence.
Keep those controls when editing a formatted translation.

`pc5_bidi.c` validates and bounds each line, protects Latin technical phrases and
path components, and uses the pinned FriBidi library for joining and bidirectional
layout. Text wraps in logical order before measurement and drawing use the same
visual glyphs. Ellipsis belongs at the left of a clipped RTL line. The loading
screen omits its Latin letter spacing for Arabic so joining remains intact.

Noto Naskh Arabic Regular/Bold supplies Arabic and presentation-form glyphs in
the normal atlas. Game names keep their own text. Other languages retain their
existing input and layout behavior. Arabic uses console language number 21;
the in-game USB keyboard's existing fallback is unchanged.

Arabic mirrors launcher placements and horizontal navigation, not image pixels,
QR modules, game names, paths or the PC5 wordmark. The backdrop's readability
gradient follows the layout direction. Code editors remain LTR, and CPU labels
remain `32-bit` / `64-bit`. `test_ui_rtl.c` checks the actual renderer's anchors,
image orientation, mixed text, bitness labels and wordmark.

The main web page uses `lang=ar` and `dir=rtl`; code editors remain LTR. Capability
phone pages embed the same catalog and translate text nodes and status messages.
They do not add an unauthenticated language endpoint or change upload permission.

Run `tests/test.sh --shots` with `PC5_SHOTS_LANG=ar`, then inspect its screenshots.
This includes joining/mixed-path bounds and concurrency checks, actual Arabic
selection in Settings, and Arabic HTTP dictionaries/phone pages. Test the final
app on a PS5 before requesting a merge, including Home, Compatibility, Settings,
media selection/removal, QR uploads, loading, and starting/exiting known games.

For console review, record the exact build commit and firmware, enable Keep all
logs, and keep the session ZIP. Check physical Left/Right and L1/R1 after changing
between Arabic and English; Compatibility's L2 Search / R2 Clear shortcuts retain
their original buttons. Check background image/video orientation and QR scanning,
and confirm that both known games still start and return to PC5.

For an installable app, build a matching full upstream runtime. The new official
main also contains Wine surround-audio changes; an Arabic eboot from this branch
must not simply be combined with an older release's runtime.
