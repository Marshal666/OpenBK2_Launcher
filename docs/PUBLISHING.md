# Publisher toolkit

Python 3.11+ and `python -m pip install -r tools/requirements.txt` are developer
requirements only. No Python is bundled into or required by the launcher.

## What the input and output paths mean

The publisher takes **unpacked, complete game installations** as input. It scans
their files and creates its own ZIP packages plus a signed manifest. There is no
command to register arbitrary existing ZIP files or assign their URLs directly
to launcher buttons.

| Argument / placeholder | Meaning | Concrete Windows example |
|---|---|---|
| `--windows-root <root>` | Folder containing the complete Windows game layout, including `bin/Game.exe` and `Data/`. | `C:/OpenBK2-Publishing/input/windows` |
| `--linux-root <root>` | Folder containing the complete Linux game layout, including `bin/Game` and `Data/`. | `C:/OpenBK2-Publishing/input/linux` |
| `--languages-dir <languages>` | Folder containing unpacked `texts-en.pak`, `texts-de.pak`, etc., directly inside it. At least one language is required. | `C:/OpenBK2-Publishing/input/languages` |
| `--out <empty-output>` | Local release staging folder created/populated by `build`. It must be absent or empty and outside both game roots. | `C:/OpenBK2-Publishing/releases/0.1.0` |
| `--release-dir <output>` | That same staging folder, reused by `sign`, `verify` and `publish-github`. | `C:/OpenBK2-Publishing/releases/0.1.0` |
| `--previous-release <prior-output>` | The retained publisher output for the preceding release, including metadata and ZIPs. | `C:/OpenBK2-Publishing/releases/0.1.0` when building `0.2.0` |
| `--base-url` | Future public URL prefix for this release's generated ZIPs. It is recorded in the manifest during `build`. | `https://github.com/YOUR_ACCOUNT/OpenBK2-Game-Releases/releases/download/v0.1.0` |

The roots are your release-preparation folders, not the launcher's source folder,
not the `bin/` subfolder alone, and not a ZIP filename. They are also independent
of the installation folder a player later chooses. A root-relative file such as
`Data/base.pak` becomes `<player-selected-folder>/Data/base.pak`.

Keep these input/output folders outside the launcher checkout so game files do
not become part of the launcher's source repository. Commands below run from
the launcher checkout on Windows; on Linux substitute ordinary absolute paths,
such as `/home/you/openbk2-publishing/input/windows`.

## Preparing bin.zip, data.zip, profiles.zip and extras.zip

Suppose you have a Windows `bin.zip`, a Linux `bin.zip`, and the other archives
are shared. Assemble two complete destination trees before calling the publisher:

| Your archive | Where its unpacked game files belong |
|---|---|
| Windows `bin.zip` | Only `input/windows/bin/`, including `Game.exe` and Windows DLLs. |
| Linux `bin.zip` | Only `input/linux/bin/`, including `Game` and Linux libraries. |
| `data.zip` | `input/windows/Data/` and `input/linux/Data/`. Put the same shared files in each. |
| `profiles.zip` | Only required official baseline files in each platform's `Profiles/`; see the opt-in rule below. Personal saves/settings must not be distributed as official files. |
| `extras.zip` | Merge its files into their intended game-relative locations in each applicable tree, e.g. `Editor/` and `splash.bmp`. A Windows-only extra goes in the Windows tree only. |

Inspect each archive's layout when extracting. If `data.zip` already contains
`Data/base.pak`, extract it at the game root. If it contains `base.pak` directly,
extract it into `Data/`. The final result must be `input/windows/Data/base.pak`,
not `input/windows/Data/Data/base.pak` or `input/windows/Complete/Data/base.pak`.
The ZIP's name does not determine installation paths.

For example, the final preparation folders could look like this. Names such as
`base.pak`, `WindowsOnly.dll`, `libLinuxOnly.so`, `editor.dat` and `official.dat`
are illustrative; keep the actual filenames and paths the game expects.

```text
C:/OpenBK2-Publishing/
  input/
    windows/
      bin/
        Game.exe
        WindowsOnly.dll
      Data/
        base.pak
      Editor/
        editor.dat
      Profiles/
        official.dat
      splash.bmp
    linux/
      bin/
        Game
        libLinuxOnly.so
      Data/
        base.pak
      Editor/
        editor.dat
      Profiles/
        official.dat
      splash.bmp
    languages/
      texts-en.pak
      texts-de.pak
  releases/
    # build creates 0.1.0 here; do not copy source ZIPs into it
```

Copy shared files into both roots as ordinary files; symlinks and hardlinks are
rejected. A file present only in the Linux root is Linux-only. The same relative
path may contain different bytes on each platform. The launcher chooses its
native platform's inventory automatically; you do not maintain a separate
launcher configuration entry for every file.

**Current sharing limitation:** snapshot and patch ZIPs are platform-specific.
Identical `Data/` files are packaged in both platforms' snapshots, so storage on
the release host is duplicated; a player downloads only their own platform's
packages. Language ZIPs are shared (`platform: any`). There is currently no
`--common-root` option or shared base-data ZIP referenced by both platforms.
Reusing one hosted `data.zip` for both would require a publisher/manifest/client
extension; it cannot be enabled by adding a URL to the present manifest.

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

The current CLI applies the same explicit allowlist to both roots, so each
allowlisted path must exist in both when publishing both platforms. If no official
profile files are required, omit `profiles.zip` from the release inputs and omit
the option entirely. Empty folders are not packaged as payloads.

Extract language packs into `input/languages/` as individual `texts-<locale>.pak`
files. If `data.zip` includes them, copy those files into that folder: `Data/texts-*`
is excluded from base snapshots automatically. The language folder must contain
only these PAK files, not ZIPs, a `Data/` wrapper, or unrelated files. The player
selects one managed language pack, and the launcher installs it under `Data/`.

## Build, sign and verify the first release

Replace `YOUR_ACCOUNT` with your GitHub account or organization, and choose your
asset repository name before building. The following build command matches the
example tree, including the hypothetical official profile file:

```text
python tools/release.py build --version 0.1.0 --sequence 1 --windows-root "C:/OpenBK2-Publishing/input/windows" --linux-root "C:/OpenBK2-Publishing/input/linux" --languages-dir "C:/OpenBK2-Publishing/input/languages" --managed-user-file Profiles/official.dat --base-url https://github.com/YOUR_ACCOUNT/OpenBK2-Game-Releases/releases/download/v0.1.0 --created-utc 2026-10-13T08:00:00Z --out "C:/OpenBK2-Publishing/releases/0.1.0"
```

This is a local operation; it does not upload anything. The output contains
`manifest.payload.json`, `release-metadata.json`, platform snapshot ZIPs named
`snapshot-windows-x64-<sha256>.zip` / `snapshot-linux-x64-<sha256>.zip`, and
language ZIPs named `language-any-<sha256>.zip`. The builder chooses package
boundaries, so they need not match your original four source archives. `sign`
adds `manifest.envelope.json`, which authenticates the exact payload bytes.

For the first release, create a secure directory outside this checkout, for
example `C:/OpenBK2-Secrets`, then generate a key once. The keygen parent directory
must already exist. Save the printed public key; replace `PUBLIC_KEY_HEX` below
with that 64-character hexadecimal value. Reuse this key for subsequent releases.

```text
python tools/release.py keygen --output-private "C:/OpenBK2-Secrets/release.pem"
python tools/release.py sign --release-dir "C:/OpenBK2-Publishing/releases/0.1.0" --private-key "C:/OpenBK2-Secrets/release.pem" --key-id release-key-2026-01
python tools/release.py verify --release-dir "C:/OpenBK2-Publishing/releases/0.1.0" --key-id release-key-2026-01 --public-key PUBLIC_KEY_HEX
```

Keep the private key outside Git, in a secure directory with restrictive ACLs
on Windows / permissions on Linux. Back it up securely. The keygen command
refuses existing files and repository destinations and prints only the public
key. Signing also accepts `OPENBK2_SIGNING_KEY_FILE`; never pass key bytes on a
command line. Production verification keys must be supplied independently,
not taken from untrusted downloaded release metadata.

For mutable/generated files elsewhere, use `--exclude <glob>`, matching paths
relative to each root, e.g. `--exclude "bin/*.log"`. Symlinks, reparse points,
hardlinked regular files, reserved paths and output directories nested in source
roots are rejected. Private source files are not auto-discovered outside supplied
roots.

## Subsequent releases and package limits

Prepare the complete latest contents of both input roots, even when only a few
files changed. Build into a **new** output directory with a new immutable tag,
version and increasing sequence. For example:

```text
python tools/release.py build --version 0.2.0 --sequence 2 --windows-root "C:/OpenBK2-Publishing/input/windows" --linux-root "C:/OpenBK2-Publishing/input/linux" --languages-dir "C:/OpenBK2-Publishing/input/languages" --managed-user-file Profiles/official.dat --base-url https://github.com/YOUR_ACCOUNT/OpenBK2-Game-Releases/releases/download/v0.2.0 --created-utc 2026-10-14T08:00:00Z --previous-release "C:/OpenBK2-Publishing/releases/0.1.0" --out "C:/OpenBK2-Publishing/releases/0.2.0"
```

Sign, verify and publish the new output as above. Retain the previous output;
do not edit its generated ZIPs or manifests. The publisher
validates all previous package bytes and computes SHA-256 differences, retains
available earlier edges and assets, and tests each advertised route against the
new final inventory. Previous-release metadata is a local publisher input,
not a remote trust anchor. Do not take it from an untrusted third party.

ZIP entries are sorted, use fixed 1980 timestamps and modes, and use deflate
level 9. Filenames include the complete archive digest. `--created-utc` or
`SOURCE_DATE_EPOCH` fixes payload timestamps (the fallback is the Unix epoch).
Same toolchain/input yields identical ZIP and payload bytes; different zlib
versions can change deflate output. Determinism is tested separately from
Ed25519 authenticity. Packages are split below 1.5 GiB, reserving ZIP overhead.
A single too-large file is rejected; no multipart-file mechanism is implied.

## GitHub hosting and the launcher's endpoint

Use an existing public asset repository, e.g. `YOUR_ACCOUNT/OpenBK2-Game-Releases`,
and an existing manifest repository with a `main` branch, e.g.
`YOUR_ACCOUNT/OpenBK2-Manifest`. These may be the same repository. The publisher
creates/releases asset releases and updates the manifest file; it does not create
repositories. Package and manifest URLs must be readable without a player's token.
The default fixed endpoint uses raw GitHub content, so GitHub Pages setup is not
required for this workflow.

Publishing uses GitHub's REST API from Python. Set `GITHUB_TOKEN` through your
secret manager or trusted CI environment. It needs Contents write access to the
asset repository and the manifest repository. Public downloads never receive it.

```text
python tools/release.py publish-github --release-dir "C:/OpenBK2-Publishing/releases/0.1.0" --owner YOUR_ACCOUNT --repo OpenBK2-Game-Releases --tag v0.1.0 --manifest-repo YOUR_ACCOUNT/OpenBK2-Manifest --manifest-branch main --key-id release-key-2026-01 --public-key PUBLIC_KEY_HEX --dry-run
```

Dry-run fully verifies the local release and prints a plan without creating a
network client or requiring a token. Remove `--dry-run` to publish. The default
manifest path is `manifest.envelope.json`; `--manifest-path` can override it.
The printed raw GitHub URL is the fixed endpoint to compile into the launcher.
Build each new release with its immutable GitHub `--base-url` shown above.
Historical patch URLs retain their original release tag.

The resulting hosting layout is:

| Location | Contents |
|---|---|
| GitHub Release `OpenBK2-Game-Releases`, tag `v0.1.0` | Generated snapshot/language ZIP assets, with the exact names chosen by `build`. Later releases add patch assets. |
| `OpenBK2-Manifest`, branch `main`, file `manifest.envelope.json` | Fixed signed endpoint pointing to the latest release's packages. |
| Your local `C:/OpenBK2-Publishing/releases/0.1.0` | All publisher output retained for verification and building subsequent patches. |

You do not upload your original four ZIPs as a substitute for the generated
packages, and players do not need `release-metadata.json`. Do not rename/repack
generated assets after signing: filenames, URLs, sizes and hashes are recorded
in the signed payload. The publishing command handles the required upload order.

Finally, compile **both production launcher builds** with the same fixed URL,
key ID and public key. For this example the configure command is:

```text
cmake -S . -B build/production -DOBK_FIXTURE_BUILD=OFF -DBUILD_TESTING=OFF -DOBK_MANIFEST_URL=https://raw.githubusercontent.com/YOUR_ACCOUNT/OpenBK2-Manifest/main/manifest.envelope.json -DOBK_KEY_ID=release-key-2026-01 -DOBK_PUBLIC_KEY_HEX=PUBLIC_KEY_HEX
```

Use the native Windows/Linux toolchain and MinSizeRel build/install commands from
the [README](../README.md). For example, after the configure command above, a
Windows Visual Studio build uses:

```text
cmake --build build/production --config MinSizeRel --parallel 1
cmake --install build/production --config MinSizeRel --prefix dist/production
```

For a Linux Ninja build, add `-G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel` to configure,
then build/install `build/production` on Linux. Fixture CI binaries intentionally use development
configuration; publishing game assets does not automatically reconfigure them.
Keep this manifest URL and trust key stable: future game releases only update
the fixed signed envelope, and existing production launchers discover the new
sequence without being rebuilt.

The command authenticates the existing fixed envelope and checks sequence
monotonicity, creates/resumes a draft release, uploads missing immutable assets,
checks GitHub's upload digest, publishes the asset release, then independently
streams every public package and verifies length/SHA-256. Only after all public
assets pass does it update the fixed envelope using the current file SHA as a
concurrency guard. It re-fetches and verifies the fixed public envelope last.
Existing assets are never overwritten or deleted. An existing asset without an
API digest is refused conservatively. Partial drafts can be resumed; a missing
asset on an already published release requires a new immutable tag. A failure
after attempting the fixed-envelope update is a **blocking publication incident**:
even a lost PUT response can mean the update committed. The publisher retries
transient public fetch failures and correctly signed stale CDN responses with
bounded backoff; a different same/newer payload is never overwritten. If public
verification still fails, inspect visibility and rerun the same verified release
as a forward repair. Never publish an older sequence to hide the incident.

## Release checklist

1. Build/test both native launchers, inspect OS dependencies and enforce 15 MiB.
2. Assemble authorized built game roots, explicit mutable-file exclusions and
   exact official MODs/Profiles opt-ins. Keep user files out of source inputs.
3. Build against the prior verified local release and a new tag/sequence. Use a
   fixed release timestamp. Verify snapshots, language packs and advertised chains.
4. Sign with the offline/external key; verify with an independently trusted public
   key. Review the payload, inventories, sizes and final launch profiles.
5. Run publication dry-run, then publish using protected CI or a release account.
6. Verify the public fixed endpoint and packages from another machine; run a
   small authorized real-game installation before announcing the release.
7. Retain earlier immutable patch assets while a latest manifest references them.

API contracts: [releases](https://docs.github.com/en/rest/releases/releases),
[assets](https://docs.github.com/en/rest/releases/assets), and
[repository contents](https://docs.github.com/en/rest/repos/contents).
Tests use a transport double with failures at every publication boundary; no
live GitHub publication was performed during implementation.
