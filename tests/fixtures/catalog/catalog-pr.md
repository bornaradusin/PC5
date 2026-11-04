**Title ID:** PPSA11018
**New listing, update or reservation:** update
**What the app does:** PC5 runs your own PC games on the PS5 through Wine, DXVK and VKD3D-Proton. 2.2.9 fixes keys that stayed pressed with Keyboard & mouse.
**Category:** emulators
**Fork or derivative of an app in the catalog?** no
**Is `source_repo` yours?** yes

- [x] The app is a native PS5 app (its own title with `eboot.bin` and `sce_sys/`), not a payload, PS4 package or backport.
- [x] I am opening this PR from the account that owns `source_repo` (or I am a public member of the owning organization), or I answered "no" above and understand a maintainer must approve it by hand.
- [x] The artifact is a `.zip` of the app folder, the only format accepted at the moment ([artifact formats](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/artifact-formats.md)) whose app uses this title ID.
- [x] For a release: the artifact is attached to a published GitHub release, and `version` is that release's tag (optionally without a leading `v`).
- [x] For a release: `sha256` is the digest of the exact asset in `artifact_url`, and I won't replace that asset.
- [x] For a release: `contentVersion` in my repository's `sce_sys/param.json` is in the `01.000.070` format and higher than my previous release's, so consoles see updates ([App versions](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/versioning.md)).
- [x] The record's `category` is the shelf people would look in first ([categories](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/metadata.md#categories)).
- [x] I have the right to distribute this build under the stated license (for another developer's repository: its license allows redistribution).
- [x] If it builds on an app in the catalog, it is a distinct app (not only a new name, theme or icon) and its name doesn't include that app's name.
- [x] I have read the [review policy](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/review-policy.md).
