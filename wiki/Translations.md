PC5's screens and its [web page](Web-page) can show in every language the PS5 has, but Arabic and Thai. PC5 follows the console's language; **Settings › System › Language** chooses another, or **System default** to follow the console again. The change shows at once.

Each language's text is one file in the repository's [`lang`](https://github.com/bornaradusin/PC5/tree/main/lang) folder: `de.po` for German, `ja.po` for Japanese, `pt-BR.po` for Brazilian Portuguese and so on. A text that isn't translated yet shows in English.

## Fixing or adding a translation

1. Download the language's file from [`lang`](https://github.com/bornaradusin/PC5/tree/main/lang) (open the file, then **Download raw file**).
2. Open it with [Poedit](https://poedit.net/) (free), or any text editor. Each text has its English (`msgid`) and its translation (`msgstr`): fill in or change the translation, and leave the English as it is.
3. Send it back:
   - as a **pull request** that changes that file (GitHub's **Edit** button on the file does it for you), or
   - as an **[issue](https://github.com/bornaradusin/PC5/issues/new)** with the file attached (zip it first, GitHub doesn't take `.po` files on their own), saying which language it is.

The translation goes into the next release.

### Things to know

- **Keep the `%s` and `%d`.** They are filled in by PC5: a game's name, a size, a number. A translation needs the same ones as the English. To put them in another order, number them: `%2$s` is the second one, `%1$d` the first (Poedit checks this). A translation whose `%` parts don't match is shown in English.
- **Keep it about as long as the English** where the note above the text says space is short: button hints at the bottom of the screen, the tabs along the top and the values on the right of a menu row. Text that doesn't fit is cut short with "…".
- **Fuzzy** translations (Poedit's "Needs work") show in English until the mark is taken off. PC5 marks a translation fuzzy itself when the English it was made for changes a little.
- Texts with the context **web** are the web page's.
- Names stay as they are: PC5, Steam, GitHub, DirectX, Vulkan, the buttons' names (L1, R1, OPTIONS).

### Trying it on the console

A changed file can be tried before it is released: copy it over the one in PC5's app folder, `PPSA11018/pc5/lang/` on the console, the way you copy other files to the console (PC5's own file manager and FTP server can't change its app folder), then close PC5 and open it again. PC5's next update puts its own file back (and, as a file of the app changed, downloads the whole app rather than the update zip).

## For developers

PC5's text is marked in `title/` with `T("...")`, `N_("...")` and `pc5_tf(...)` (see `title/pc5_i18n.h`). After changing text, run `tools/update-po.py`: it makes `lang/pc5.pot` from the sources again and merges it into every `lang/*.po`, keeping their translations. `tests/test.sh` checks that it was run.
