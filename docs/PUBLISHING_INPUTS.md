# Publishing input and package reference

Start with the numbered [publishing guide](PUBLISHING.md) for repository setup,
signing keys, your first release and later updates. Use this reference when
preparing game files, official profiles or optional mods.

The example path `C:/Users/you/OpenBK2-Publishing` corresponds to
`$PublishingRoot` in that guide. Substitute your own user folder.

## What the input and output paths mean

The publisher takes **unpacked game files** as input. Use a shared folder for
common files and a separate folder for each platform's files. Together, shared
plus platform files must form a complete game installation. The publisher scans
them and creates its own ZIP packages plus a signed manifest. There is no
command to register arbitrary existing ZIP files or assign their URLs directly
to launcher buttons.

| Argument / placeholder | Meaning | Concrete Windows example |
|---|---|---|
| `--shared-root <root>` | Optional common game files. Packaged once and referenced by both platforms. | `C:/Users/you/OpenBK2-Publishing/input/shared` |
| `--windows-root <root>` | Windows-only files in their game-relative layout, including `bin/Game.exe`. Without a shared root, supply a complete Windows installation. | `C:/Users/you/OpenBK2-Publishing/input/windows` |
| `--linux-root <root>` | Linux-only files in their game-relative layout, including `bin/Game`. Without a shared root, supply a complete Linux installation. | `C:/Users/you/OpenBK2-Publishing/input/linux` |
| `--mods-dir <mods>` | Optional downloadable mods, one subfolder per mod. These are separate from the base game. | `C:/Users/you/OpenBK2-Publishing/input/mods` |
| `--languages-dir <languages>` | Folder containing unpacked `texts-en.pak`, `texts-de.pak`, etc., directly inside it. At least one language is required. | `C:/Users/you/OpenBK2-Publishing/input/languages` |
| `--out <empty-output>` | Local release staging folder created/populated by `build`. It must be absent or empty and outside the shared/platform/mod input folders. | `C:/Users/you/OpenBK2-Publishing/releases/0.1.0` |
| `--release-dir <output>` | That same staging folder, reused by `sign`, `verify` and `publish-github`. | `C:/Users/you/OpenBK2-Publishing/releases/0.1.0` |
| `--previous-release <prior-output>` | The retained publisher output for the preceding release, including metadata and ZIPs. | `C:/Users/you/OpenBK2-Publishing/releases/0.1.0` when building `0.1.1` |
| `--base-url` | Future public URL prefix for this release's generated ZIPs. It is recorded in the manifest during `build`. | `https://github.com/YOUR_ACCOUNT/OpenBK2-Game-Releases/releases/download/v0.1.0` |

The roots are your release-preparation folders, not the launcher's source folder,
not the `bin/` subfolder alone, and not a ZIP filename. They are also independent
of the installation folder a player later chooses. A root-relative file such as
`Data/base.pak` becomes `<player-selected-folder>/Data/base.pak`.

Keep these input/output folders outside the launcher checkout so game files do
not become part of the launcher's source repository. Examples use Windows paths;
when publishing on Linux, substitute ordinary absolute paths,
such as `/home/you/openbk2-publishing/input/windows`.

## Preparing bin.zip, data.zip, profiles.zip and extras.zip

Suppose you have a Windows `bin.zip`, a Linux `bin.zip`, and the other archives
are shared. Prepare the following folders before calling the publisher:

| Your archive | Where its unpacked game files belong |
|---|---|
| Windows `bin.zip` | Only `input/windows/bin/`, including `Game.exe` and Windows DLLs. |
| Linux `bin.zip` | Only `input/linux/bin/`, including `Game` and Linux libraries. |
| `data.zip` | `input/shared/Data/`. Each shared file is supplied and hosted once. |
| `profiles.zip` | Only required official baseline files in `input/shared/Profiles/`; see the opt-in rule below. Personal saves/settings must not be distributed as official files. |
| `extras.zip` | Common extras in their game-relative locations under `input/shared/`, e.g. `Editor/` and `splash.bmp`. A Windows-only extra goes in the Windows tree only. |

Inspect each archive's layout when extracting. If `data.zip` already contains
`Data/base.pak`, extract it at the game root. If it contains `base.pak` directly,
extract it into `Data/`. The final result must be `input/shared/Data/base.pak`,
not `input/shared/Data/Data/base.pak` or `input/shared/Complete/Data/base.pak`.
The ZIP's name does not determine installation paths.

For example, the final preparation folders could look like this. Names such as
`base.pak`, `WindowsOnly.dll`, `libLinuxOnly.so`, `editor.dat` and `official.dat`
are illustrative; keep the actual filenames and paths the game expects.

```text
C:/Users/you/OpenBK2-Publishing/
  input/
    shared/
      Data/
        base.pak
      Editor/
        editor.dat
      Profiles/
        official.dat
      splash.bmp
    windows/
      bin/
        Game.exe
        WindowsOnly.dll
    linux/
      bin/
        Game
        libLinuxOnly.so
    languages/
      texts-en.pak
      texts-de.pak
    mods/
      MyMod/
        mod.pak
        assets/
          example.txt
      AnotherMod/
        mod.pak
  releases/
    # build creates 0.1.0 here; do not copy source ZIPs into it
```

Use ordinary files; symlinks and hardlinks are rejected. A file present only in
the Linux root is Linux-only. A path present in `shared` must **not** also appear
in a platform root: duplicate paths, case aliases and file/directory conflicts
are rejected, even if their bytes match. There is no implicit override order.
If the same path needs different bytes on Windows and Linux, remove it from
`shared` and put the appropriate copy in each platform root instead.

The launcher automatically combines the shared packages with its native
platform's packages. Each shared ZIP has one URL and one record in the manifest;
both targets reference it. Shared patches are also reused between platforms
when they need the same changed files. Identical content is not automatically
deduplicated across two platform roots: put it in the shared root explicitly.

Do not leave the original `bin.zip`, `data.zip`, etc. in the game roots unless
the game itself actually needs those ZIP files installed. The publisher scans
ordinary files; it does not recursively unpack a ZIP found inside a root.

The current CLI generates launch profiles for **`bin/Game.exe` on Windows and
`bin/Game` on Linux**, with `bin/` as the working directory. Supply those actual
built game executables. Other executable names need an explicit publisher launch
profile enhancement; there is currently no CLI override for them. The main
executable is automatically marked executable. For other Linux executables,
preserve their execute bits and package on Linux if your Windows filesystem or
archive extraction tool cannot represent those permissions.

## Profiles, MODs and language files

`Profiles/` and `MODs/` are user-owned by default: their files are **excluded even
when present in an input root**. If `Profiles/official.dat` is a required official
baseline, explicitly add `--managed-user-file Profiles/official.dat` to `build`.
Repeat the option for each exact official file; directory-wide opt-ins and
wildcards are not supported. Do not opt in ordinary player saves or settings.
The launcher preserves unknown files and backs up modified official files before
replacing them.

The CLI applies the same explicit allowlist to both targets, so each allowlisted
path must exist in each combined shared-plus-platform inventory. A shared
official profile needs only one source copy and one opt-in. If no official
profile files are required, omit `profiles.zip` from the release inputs and omit
the option entirely. Empty folders are not packaged as payloads.

Extract language packs into `input/languages/` as individual `texts-<locale>.pak`
files. If `data.zip` includes them, copy those files into that folder: `Data/texts-*`
is excluded from base snapshots automatically. The language folder must contain
only these PAK files, not ZIPs, a `Data/` wrapper, or unrelated files. The player
selects one managed language pack, and the launcher installs it under `Data/`.

## Optional mod downloads

Pass `--mods-dir "C:/Users/you/OpenBK2-Publishing/input/mods"` for the example above.
Each immediate subfolder becomes a catalog entry. `MyMod/mod.pak` is installed
as `<game-root>/MODs/MyMod/mod.pak`; **do not add another `MODs/` wrapper** under
the input folder. Loose assets work the same way. Mod IDs/folder names must use
letters, digits, `_` or `-`, start with a letter or digit, and be at most 64
characters. Empty mods, links and special files are rejected. The folder name
is also its displayed label. This initial catalog serves the same mod bytes to
Windows and Linux; platform-specific mod variants are not implemented.

The publisher creates `mod-any-<sha256>.zip` archives and signs each mod's exact
file inventory within the game manifest. They are uploaded and publicly verified
alongside the game packages before the fixed manifest is updated. Normal game
installation downloads no mod archives. Players open **Mods**, then **Download**.
They must first update the game to the catalog's release and close the game.
Only one mod folder can be present; remove the current one before downloading
another. Existing manually added folders are listed too.

**Remove** asks for confirmation, then moves the entire selected folder,
including player edits and extra files, to
`.launcher/backups/removed-mods/<unique-id>/<mod-id>/`. It does not delete it.
To restore a backup manually, close the game and launcher, ensure `MODs` has no
other mod folder, and move that folder back to `MODs/<mod-id>/`.
Protected base-game folders cannot be removed through this control. Folders
with unsafe paths or links require manual inspection. Backups use the same disk;
archive old backups manually if the updater's 8 GiB retention limit is reached.

Optional mods do **not** need `--managed-user-file`; they have their own signed
ownership scope. Do not also put them in shared/platform roots as base files.
Base-game repair and updates preserve downloaded mods and player edits. They
do not automatically update or verify an edited mod. To obtain a changed mod,
publish a new release sequence, update the game, then Remove and Download it.
Every release catalog is complete: continue supplying `--mods-dir` to keep its
entries downloadable. Omitting it removes catalog entries without deleting
players' already installed folders. There is no mod editor or enable/disable UI.

## Package limits and reproducibility

The builder creates full snapshots on every release and complete-file patches
when `--previous-release` is supplied. A patch contains whole changed files;
changing a large PAK can therefore require downloading that entire PAK again.

ZIP entries are sorted, use fixed 1980 timestamps and modes, and use deflate
level 9. Filenames include the complete archive digest. `--created-utc` or
`SOURCE_DATE_EPOCH` fixes payload timestamps; the fallback is the Unix epoch.
The same toolchain and inputs produce identical ZIP and payload bytes; different
zlib versions can change deflate output. Packages split below 1.5 GiB with space
reserved for ZIP overhead. A single file that cannot fit is rejected; files are
not split into multiple parts.

Shared data or optional mods produce [manifest v2](MANIFEST_V2.md), requiring
launcher **0.2.0 or newer** regardless of the game version. Update older launcher
executables before publishing v2 to their endpoint. Without either feature, a
new release lineage uses [v1](MANIFEST_V1.md); an existing v2 lineage stays v2.
Launcher 0.2.0 supports both schemas.

For mutable or generated files elsewhere in a game root, add repeatable
`--exclude <glob>` options, such as `--exclude "bin/*.log"`. Patterns match
root-relative paths. Review the inventory before signing. Keep private material
outside every input root; the publisher packages ordinary files it finds there.

[Return to the publishing guide](PUBLISHING.md).
