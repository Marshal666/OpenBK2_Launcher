# Signed manifest v2: shared packages and optional mods

This extends [v1](MANIFEST_V1.md), using the same Ed25519 envelope version 1,
canonical base64, exact signed payload bytes, strict field validation, immutable
package hashes, limits, portable paths and monotonic release sequence. Nothing
changes about the trusted key source or HTTPS requirements.

The payload's `schema_version` is `2`, `min_launcher_version` must be at least
`0.2.0`, and the root must additionally contain `mods` (an object, possibly empty).
Launcher 0.2.0 accepts both schemas. A v1 payload cannot use these new features.

## Shared base packages

`snapshot` and `patch` packages may have `platform: "any"`. A platform's
`snapshot_package_ids` or an edge's `package_ids` may reference those packages
alongside its own platform-specific packages. Other-platform references remain
invalid. Every package still has exactly one record and URL in `packages`.

Each platform's `files` remains a **complete** target inventory. The disjoint
union of its referenced snapshots must exactly cover its bytes. Cross-package
path conflicts and shared/platform overrides are forbidden. Snapshots for both
platforms can reference the same shared package. An update edge references only
the shared patches it needs; migration from asymmetric v1 inventories is valid.
Identical content-addressed patches reused on several historical edges are
downloaded once, while route simulation still applies each edge in order.

## Mod catalog

Each `mods` key is a portable folder ID matching
`[A-Za-z0-9][A-Za-z0-9_-]{0,63}`. IDs cannot collide after ASCII case folding.
There are at most 256 entries. An entry has exactly these fields:

```json
{
  "label": "MyMod",
  "files": [
    {"path": "MODs/MyMod/content.pak", "size": 1234,
     "sha256": "<64 lowercase hex characters>", "executable": false}
  ],
  "package_ids": ["mod-any-<archive-sha256>"]
}
```

The illustrative digest placeholders above are not valid digest values. The
label must contain 1–128 UTF-8 bytes. Files must be nonempty, total at most
64 GiB, and all reside strictly within `MODs/<id>/`. The folder cannot overlap
any platform's base inventory, even if bytes match. Existing protected launcher
paths and basenames remain prohibited. Every mod package has `kind: "mod"`,
`platform: "any"`, and the ordinary package fields from v1. Its ZIP file list
is checked in full; the disjoint union of referenced package inventories must
exactly match the catalog entry. Unreferenced packages are invalid.

These are publisher-selected, signed downloads; arbitrary URLs and unsigned
catalogs are not supported. Their bytes are common to both platforms. Optional
mod files are **not** added to the installed base-game inventory or its exact
user-directory allowlist. Player edits after installation are allowed. Download
requires the catalog's payload hash to equal the authenticated installed game
manifest; a newer catalog first requires a game update. No separate editable
mod receipt can grant deletion authority. Removal is an explicit user action
that retains the selected folder in a backup, including manually installed mods.

See [publishing instructions](PUBLISHING.md#optional-mod-downloads) and the
[folder transaction protocol](UPDATE_PROTOCOL.md#optional-mod-folder-operations).
