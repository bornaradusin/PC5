# Replies waiting for a release

A reply to an issue that a fix in the next release answers waits here until that release is
published: then `.github/workflows/release-replies.yml` posts it on its issue (while the issue
is open), labels the issue **fix ready**, asks the closed issues of the same game to try again,
and deletes the file from main.

One file an issue, named by its number (`433.md`):

```text
version: 2.3.0

Thanks for the log! … PC5 2.3.0 fixes it. Please update and attach the new zip either way.

If the game works now, please close this issue: that adds it to the game compatibility list.
```

- The first line is `version: X.Y.Z`, the release it waits for (its tag without the `v`).
- Then a blank line, then the reply exactly as it is to be posted (Markdown).
- A reply for an issue closed before the release is published isn't posted; its file goes too.
- A pre-release posts nothing.

The posted reply counts as the maintainer's for the two days a reporter has to answer
(`.github/scripts/no_reply.py`).
