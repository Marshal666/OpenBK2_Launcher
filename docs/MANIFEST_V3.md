# Signed manifest v3: initial user data

V3 extends [v2](MANIFEST_V2.md), retaining the exact Ed25519 envelope, HTTPS,
SHA-256, path, archive and replay rules. `schema_version` is `3` and
`min_launcher_version` is at least `0.3.0`. Both `mods` and `initial_data` are
required, including when empty. V1/v2 remain readable by launcher 0.3.0.

The additional root object has exactly these fields:

```json
{
  "initial_data": {
    "folders": ["Editor", "Profiles"],
    "files": [
      {"path": "Editor/default.cfg", "size": 10,
       "sha256": "<64 lowercase hex characters>", "executable": false},
      {"path": "Profiles/default.cfg", "size": 20,
       "sha256": "<64 lowercase hex characters>", "executable": false}
    ],
    "package_ids": ["initial-any-<archive-sha256>"]
  }
}
```

Digest placeholders above are illustrative. Initial packages use `kind:
"initial"`, `platform: "any"` and ordinary v1 package fields. The disjoint union
of their files must match the signed initial inventory exactly. There are at
most 64 folders, 100,000 files and 64 GiB of uncompressed initial data, subject
to the ordinary per-file/package limits. All files have `executable: false`.
Folder names are unique portable top-level path components; every listed
folder must contain a file. Empty initial data uses three empty arrays.

`bin`, `MODs`, `.launcher`, `launcher_installed.txt` and launcher names
are prohibited, case-insensitively. All files must be strictly inside one listed
folder. No platform's managed inventory may contain any path within those
folders, except for `Data/` with `min_launcher_version >= 0.3.1`. Data uses
file-level ownership: initial files may share directories with managed files,
but the combined initial/platform/all-language inventory must have no duplicate,
case-aliased or file/directory-conflicting paths. The initial inventory is
separate from platform `files`, snapshots, patches, languages and mods.
It grants authority to supply defaults and perform
the user's explicit Reset, not to continuously reconcile mutable files.

During install, the planner adds only missing defaults for previously
uninitialized folders. The ordinary file transaction signs off newly seeded
bytes together with the base installation. Files already present are untouched.
Migration from a prior managed inventory excludes initial ownership scopes
(whole folders, or listed Data files) from obsolete-file removals. All later
automatic managed-file checks exclude initialized data.

Local state v2 adds `initialized_folders` to the v1 fields. The markers persist
across releases, including releases that omit initial data. They can only
restrict automatic writes; they cannot authorize deletion or override signed
inventories. A missing/invalid state uses safe adoption and fills only missing
defaults. V1 local states remain readable with v1/v2 manifests.

Data support uses local state v3, adding `initialized_data_files`: normalized
case-folded paths that remain protected after defaults disappear from a release.
The `Data` folder marker still prevents automatic reseeding, while managed files
at other paths continue updating. Previously initialized paths and their file/
directory ancestors or descendants cannot become managed files. States v1/v2
remain readable for installations without initial Data files.

Reset uses the authenticated **installed** manifest, so it never mixes defaults
from an uninstalled update with the current game. Its `.launcher/initial-reset.json`
journal binds the payload hash, unique operation ID, signed reset paths,
original-path presence, commit/rollback phase and backup policy. Reset journal
v2 uses `paths`: whole folders outside Data, and individual declared initial
files inside Data. It never authorizes replacing Data itself or unrelated files.
Legacy v1 `folders` journals remain recoverable for whole-folder releases.
Verified replacements and temporary originals live under `.launcher/resets/`.
Recovery needs no network. See [the recovery protocol](UPDATE_PROTOCOL.md).
