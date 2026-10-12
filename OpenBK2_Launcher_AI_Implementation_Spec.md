# OpenBK2 Launcher — Master AI Implementation Specification

**Specification version:** 1.0 (initial experimental release)  
**Project:** a NEW, standalone, open-source repository for the OpenBK2 game launcher  
**Primary game repository for reference only:** https://github.com/OpenBK2/OpenBK2  
**Status:** implementation directive; no launcher code is assumed to exist yet.

---

## 0. Instructions to the coding AI — read this first

You are the lead C++ systems developer, release engineer, and test engineer implementing **OpenBK2 Launcher**. Build a real, buildable, tested application rather than an architectural mock-up. Use the requirements and acceptance tests below as the source of truth.

1. Create an **independent repository/project**. Do not modify the OpenBK2 game repository. You may inspect it to confirm game layout and launch requirements.
2. Implement in small, reviewable milestones. At every milestone, ensure the project builds, run automated tests, and report what is implemented, what was tested, and what remains.
3. Favor correctness, clean interfaces, small binary size, and cross-platform compatibility. Avoid needless abstractions or elaborate frameworks.
4. Implement **the update engine and publisher against tiny synthetic game fixtures first**. Never use the full game as the first integration test. No real game downloads or public release publication are needed to prove core behavior.
5. Do not fake functionality: visible controls must work; signatures and hashes must really be verified; download progress must be real. Do not use hard-coded placeholder release URLs, signatures, hashes, or signing keys in a production binary.
6. Use ordinary C++20 with CMake and Python 3.11+ for **publisher scripts only**. Python is NOT a launcher runtime dependency. The publisher may use the Python `cryptography` package for Ed25519 signing, explicitly declared and pinned in a requirements file.
7. Never commit a private signing key, credential, access token, or production secret. Embed **only a public Ed25519 verification key** in the launcher.
8. Where this specification leaves a minor choice unspecified, use the simplest safe implementation, document it in an ADR or README, and continue without blocking. If a requirement proves impossible, provide evidence and a working nearest alternative rather than silently dropping it.
9. **Definition of done:** actual native Windows and Linux builds, a visible functional UI, end-to-end fixture-based installation/update/language tests, publisher scripts, documentation, and size/dependency audits. Claims of tested platform support must be backed by tests.

### Priorities if trade-offs arise

1. Never damage user data, never execute unverified downloaded code, and never trust an unsigned manifest.
2. Finish a reliable installation/update/recovery pipeline.
3. Keep the launcher responsive and actually launch the game correctly.
4. Deliver a small, attractive, single-file executable on each platform.
5. Optimize download size, cold-start time, and polish.

---

## 1. Product goals, target platforms, exclusions

Create a compact OpenBK2 launcher that downloads the entire latest game, updates an existing installation, changes the active game language, and launches the game. It must be straightforward enough that a nontechnical player can run it once, choose a folder and language, click Install, and later click Play or Update.

### Required release artifacts

- **Windows:** one self-contained **64-bit `.exe`**, with no companion launcher DLLs, script runtimes, extracted bootstrapper, `.pak` files, or additional launcher resources required. Use statically linked third-party launcher libraries wherever practical. Dependencies on ordinary Windows system components are allowed.
- **Linux:** one **x86-64 ELF executable**. Statically link application-level libraries where practical. Dependencies on libc, the normal Linux graphical stack, graphics drivers, X11/Wayland components, the system CA store, and other ordinary system facilities are permitted. Do not ship companion `.so` files.
- **Size budget:** aim for **under 10 MiB** per binary; **15 MiB is the maximum accepted release size** (15 × 1024 × 1024 bytes). Strip release symbols, use size-oriented release flags judiciously, and publish measured results. A build exceeding the hard limit fails CI; do not hide or redefine this requirement.
- **Windows 7 SP1 x64:** best-effort legacy support, including TLS 1.2 and basic rendering, but mark as **verified** only after VM testing. Current Windows and Linux have priority.
- **Linux baseline:** build against an intentionally conservative glibc/runtime baseline (for example an Ubuntu 22.04 LTS-compatible build environment or older, if achievable), and test on the oldest chosen supported distribution, on X11 and Wayland where feasible.
- **Display:** desktop app nominally **1000 × 600 logical pixels**; adapt scaling/layout gracefully to a smaller desktop or high-DPI environment.

### Explicitly out of scope for the first version

- Launcher self-update, online accounts, authentication, telemetry, advertising, background video playback, P2P distribution, game mod management, user-selectable historical releases/downgrades, multiplayer features, and binary-delta patching.
- No user-facing “Rollback version” feature. **Internal transaction recovery is mandatory** and must not be confused with historical release rollback.
- No special privileged system-wide installer or elevation requirement. Default paths must be user-writable.

---

## 2. Technology stack and dependency policy

Use:

- **C++20**, **CMake** and CTest.
- **SDL3** for cross-platform application/window/event handling, native folder selection, and process launching.
- **Dear ImGui** with its **SDL3 platform backend and SDL_Renderer3 backend** for the first version. Prefer SDL_Renderer3 over a complicated custom GPU pipeline; fall back to an alternate renderer only if testing demonstrates a concrete limitation. Do not depend on SDL_GPU/Vulkan as a requirement for a small launcher.
- **libcurl** for HTTPS, streaming downloads, redirects, request timeouts, resumable Range requests, and HTTP response handling.
- **yyjson** for bounded JSON parsing (or justify an equivalently small, maintained alternative).
- **miniz** for ZIP reading and decompression. The extraction wrapper MUST implement all ZIP security checks described below.
- **Monocypher with its optional Ed25519 module** for verification of publisher-signed manifest payloads.
- **PicoSHA2** or another audited small portable SHA-256 implementation for streaming file/package hashes. Monocypher's core hashing is not a drop-in SHA-256 implementation.
- **stb_image** or a similarly compact image decoder if needed for embedded static artwork.
- Standard C++ library plus Win32 system APIs and ordinary Linux desktop facilities for platform services.

Pin third-party versions (tags/commit SHAs), retain license notices, and avoid dynamically fetching dependencies at application runtime. License original launcher code under MIT unless the repository owner selects another compatible license; keep the OpenBK2 game’s own noncommercial license and game assets separate and do not assume the launcher’s license relicenses game data. A reproducible source build should be documented. Do NOT vendor unnecessary library modules (e.g., audio/video subsystems not used by this launcher). Prefer a TLS configuration that is small and reliable: Windows Schannel is an option, subject to Windows 7 testing; Linux may statically link a TLS library while using the system certificate store. The requirement is **no companion launcher libraries**, NOT “no OS libraries.”

Use RAII, well-defined ownership, `std::filesystem` with safe Unicode handling, bounded allocations, clear result/error types, and no raw unmanaged threads.

---

## 3. Proposed repository layout

A reasonable target layout is:

```text
OpenBK2-Launcher/
  CMakeLists.txt
  CMakePresets.json
  README.md
  LICENSE
  THIRD_PARTY_NOTICES.md
  .gitignore
  .github/workflows/build.yml
  cmake/
  include/launcher/
    application.hpp
    model.hpp
    manifest.hpp
    signature.hpp
    sha256.hpp
    release_provider.hpp
    downloader.hpp
    package_reader.hpp
    planner.hpp
    installer.hpp
    transaction.hpp
    local_state.hpp
    settings.hpp
    localization.hpp
    platform_services.hpp
    game_launcher.hpp
    ui.hpp
  src/
    main.cpp
    application.cpp
    manifest.cpp
    signature.cpp
    sha256.cpp
    github_release_provider.cpp
    http_downloader.cpp
    zip_package_reader.cpp
    update_planner.cpp
    installer.cpp
    transaction.cpp
    local_state.cpp
    settings.cpp
    localization.cpp
    game_launcher.cpp
    ui.cpp
    platform/windows.cpp
    platform/linux.cpp
  assets/
    hero.webp (or optimized embedded PNG/JPEG)
    icon.png
    fonts/ (subset and embed into executable)
    i18n/en.json
    i18n/de.json
  tools/
    requirements.txt
    release.py
    signing.py
    github_publish.py
    fixture_builder.py
  tests/
    core/
    integration/
    fixtures/
  docs/
    MANIFEST_V1.md
    UPDATE_PROTOCOL.md
    PUBLISHING.md
    SECURITY.md
    PLATFORM_TESTING.md
    ADR/
```

Adapt names as needed, but keep transport, manifest validation, planning, installation, UI, and OS integration logically separate. Core update logic must be testable **without** SDL, Dear ImGui, or a live network.

---

## 4. Game directory model and ownership policy

User selects the **game installation root**, called `OBK2` in examples, containing:

```text
OBK2/
  bin/
  Data/
  MODs/
  Profiles/
  Editor/
  splash.bmp
  launcher_installed.txt
  .launcher/
    state.json
    installed_manifest.envelope.json  # last installed, still-signature-verifiable manifest
    transaction.json           # exists only during an unfinished transaction
    downloads/
    staging/
    backups/
    cache/
  OpenBK2Launcher.exe          # only if user requests relocation, Windows example
```

`bin`, `Data`, `Editor`, and `splash.bmp` contain normal signed-manifest-managed files, but individual known game-generated files inside them can be excluded by publisher rules. **Never regard an entire directory as safe to delete.**

- `MODs/**` and `Profiles/**`: **user-owned by default**. Never recursively remove, clean, or overwrite unknown content. If the game ships essential *official baseline files* inside these folders, the publisher may opt in only those **exact individual paths** as signed-manifest-managed files using `managed_user_dir_files`. Modifications to those official files require preservation/backups before replacement. Everything else remains untracked and protected.
- `.launcher/**`: launcher-owned metadata/cache; never included inside a game ZIP.
- `launcher_installed.txt`: launcher-generated, readable status summary, **not** authoritative update state.
- The launcher executable itself: launcher-owned, never part of game manifests or packages.
- Any file not recorded as launcher-managed is **untracked** and must be preserved. If installing a managed file at an existing untracked path, do not silently destroy it: safely back it up and warn or ask the user to approve a takeover.
- Unknown files in `Data/` or `Editor/` may be user content. Preserve them unless an explicitly managed-file conflict is safely handled.
- Detect modifications of managed files by comparing actual hashes to the previously recorded expectations. When replacing a modified managed file, save a clearly identified backup in `.launcher/backups/` when feasible and report this to the user. If backup fails, do not destroy the modified file.
- Define stable protected paths in the client and validate them on the publisher side. `.launcher/**`, the launcher executable, and generated status files are never game-managed. `MODs/**`/`Profiles/**` remain protected except exact, explicitly allowlisted official files. Never let ZIP content itself determine whether user data may be deleted.
- Preserve the **previously installed signed manifest envelope** in `.launcher/installed_manifest.envelope.json` and reverify it when deciding which old paths were legitimately publisher-managed. Local `state.json` alone must not authorize deletion or overwrite of arbitrary paths if it is corrupt or tampered with.

Game ZIP paths are always **relative to `OBK2/`**, not to `Complete/`, not to `/`, and not to the user's selected parent directory.

---

## 5. First-run setup, settings, and adoption

On first launch, display a coherent setup wizard/modal BEFORE any game installation:

1. Installation folder picker; propose a **user-writable path** (e.g., user Games directory) and allow browsing. Validate writability, reasonable path length, lack of conflicts, and available disk space.
2. Launcher/game language dropdown, populated by languages in the **verified signed manifest**; show a sensible embedded fallback/initial language until the manifest becomes available.
3. Checkbox **Move launcher into installation folder** (recommended default: enabled).
4. Checkbox **Create desktop shortcut** (recommended default: enabled).
5. Confirmation with approximate download size, required working/free space, and an **Install** action.

Persist per-user launcher preferences such as selected root, GUI language, shortcut preference, and window preferences under OS config directories, not in a throwaway temporary folder:

- Windows: under `%LOCALAPPDATA%/OpenBK2Launcher/`.
- Linux: under `$XDG_CONFIG_HOME/openbk2-launcher/` or `~/.config/openbk2-launcher/`.

Store installation-specific verified state in `OBK2/.launcher/state.json` and update scratch space on the **same filesystem** as the game root, so file rename/replace behavior can be made predictable. Cache images in `.launcher/cache` or OS cache directories. Downloaded package cache is disposable; transactional staging/journal files are **not** disposable until recovery finishes.

Detect an existing game directory and offer **Adopt existing installation**. Never claim it is managed based merely on the presence of `bin/`; verify an initial set of expected files and, before managing it, reconcile a signed target manifest. Preserve all untracked data. If a previously configured root is missing, show recovery/reselect UI rather than assuming an empty installation.

### Relocation and shortcuts

On Windows an executable cannot reliably delete/replace its own running image. Relocation must copy, verify, launch the destination executable in a controlled handoff, persist settings, and perform best-effort cleanup only after the source instance exits. Avoid untrusted command-line deletion switches. On Linux use a safe analogous approach. If relocation fails, the original launcher remains usable and the user sees an error; installation itself should not be corrupted.

Create shortcuts with native OS mechanisms: Windows `IShellLinkW`/shell folder APIs and Linux `.desktop` files with properly escaped `Exec` paths and executable/trust behavior appropriate to the desktop environment. Shortcuts must point to the final launcher path, not a temporary or soon-deleted source. The launcher must also work when invoked from any working directory.

---

## 6. User interface and interactions

Make an attractive, dark-themed, fully custom, **borderless** native application. Use a 1000 × 600 logical-pixel starting layout. A possible layout:

- Top 45–55 px: logo/title, subtle version/status text, **Settings**, minimize, close; the blank title-bar regions support compositor/window-manager dragging.
- Central ~380–420 px: large **embedded static hero image**, scaled/cropped attractively; dark readability gradient/overlay; no runtime asset file needed for the default image.
- Bottom ~120–145 px: prominent **Play** button, **Install/Update** button, compact version/status/availability indicator, status text, and **real progress bar** during operations.
- Settings overlay: installation folder chooser, launcher/game language selector, create shortcut action, cache cleanup action (optional), install location/version display.
- First-run setup overlay as described above.

Use consistent spacing, typography, rounded corners, high contrast, subtle accents, hover/focus states, disabled-button explanations, and visible errors. Support keyboard navigation for main actions and correct HiDPI scaling. Do not use the default native window decorations. Use `SDL_SetWindowHitTest` and a drag area where supported, with a documented platform fallback; never implement dragging by naively setting Wayland top-level window coordinates.

### Button semantics

- **Install** (not installed): fetch/verify signed manifest, download full snapshot plus chosen language pack, stage, commit, verify, mark ready.
- **Update** (installed): fetch/verify latest signed manifest, verify local managed files as needed, download updates, and install the latest release. When already current, show **Up to date**. On normal startup, perform a **nonblocking automatic update-availability check** if networking is available; never automatically disrupt or replace a running game. A user clicking Update explicitly starts install/update. Do not require an online check for offline Play.
- **Play**: launch a valid local game even offline. Disable while install/update commit/recovery is in progress or if installation is incomplete. Do not force network access before Play.
- **Settings**: working folder selection, language switching, shortcut creation, and readable status/errors. If installation root changes, explicitly distinguish selecting an existing install from moving game files; do not silently move terabytes of game data.
- **Close during download**: offer safe cancel/exit; incomplete downloads remain resumable. **Close during commit**: do not abandon an inconsistent installation; finish safely or record recoverable transaction state and block Play on next startup until recovered.

### Progress reporting

Never render fake progress. Provide stage labels, e.g., `Checking`, `Downloading (42%, 1.2/2.9 GiB)`, `Verifying`, `Extracting`, `Installing`, `Finalizing`, `Ready`. Display bytes transferred/total, instantaneous or smoothed speed, optional ETA when credible, and stage-specific progress. If content size is unknown, display an indeterminate state. Downloads and file hashing must be performed on worker threads with thread-safe progress snapshots; all SDL/ImGui API calls stay on the UI thread.

### Localizations

The launcher's UI text is embedded at build time for declared supported locales; fall back to English for missing keys. Game translation data is a separately downloaded language ZIP. Do not confuse the two. The dropdown must never promise a complete localized launcher UI unless translations and necessary glyph coverage exist. Support Unicode in paths and locale names. Prefer a suitably subsetted embedded font, and document fallback for scripts not initially bundled.

---

## 7. Game language semantics

Every game language is one managed file with a locale-specific name, for example:

- English: `Data/texts-en.pak`
- German: `Data/texts-de.pak`

These packs are **separately packaged, versioned, SHA-256 verified, and updateable**. The signed manifest specifies which locales exist and their exact relative paths; the client must not synthesize arbitrary file paths from untrusted UI input.

Exactly **one launcher-managed language pack** is active per installation. On language change:

1. Immediately change launcher UI locale if translations are embedded, but mark the requested game language as pending.
2. Download the new game language package over HTTPS, resume if feasible, verify ZIP SHA-256 and each extracted file SHA-256.
3. Stage and commit the new pack **before removing** the previously managed pack. Use the same transaction journal as normal updates.
4. Remove only the old language file specifically recorded as launcher-managed; never delete untracked `texts-*.pak` files indiscriminately.
5. Update `state.json` only after successful commit; on failure keep the previous game language playable and show that the requested switch is pending.

A normal game update also checks whether the selected language package changed and updates it. Other languages are not downloaded unnecessarily. An offline Play action can still use the successfully installed prior language, with an informative status message if a requested switch is pending.

---

## 8. Hosting strategy and networking

**GitHub Releases** is the initial binary-package host, with a fixed HTTPS URL to a small mutable **signed manifest envelope** (GitHub Pages is an acceptable initial manifest host). Do not store game package ZIPs as ordinary huge Git blobs or force players to clone the game repository.

- The initial production manifest URL is compiled into the launcher (or a fixed securely configured release endpoint). Provide a safe **developer/test-only** override that cannot accidentally make production builds accept a test key or non-HTTPS remote code.
- Publish release assets with immutable, content-addressed filenames or unique version/hash filenames. Never mutate/reuse existing package URLs after publication.
- The manifest may refer to GitHub Releases URLs initially and future R2 URLs later. All packages must be fetched using HTTPS.
- Changing from GitHub to Cloudflare R2 must **not change** manifest verification, package verification, update planning, installation, GUI, or game-launch code.

### Strategy pattern

Expose a minimal provider interface, such as:

```cpp
class IReleaseProvider {
public:
    virtual ~IReleaseProvider() = default;
    virtual Result<ByteBuffer> fetchLatestEnvelope(CancellationToken&) = 0;
    virtual Result<ResolvedPackageLocation> resolvePackage(const PackageRecord&) = 0;
};
```

Implement `GitHubReleaseProvider` initially. An eventual `R2ReleaseProvider` can reuse the same `HttpDownloader`. If a signed package record already carries a valid HTTPS URL, `resolvePackage()` may simply return it. **Do not** make host-specific code responsible for interpreting manifests, selecting packages, or writing game files. Depend on the interface via constructor injection in the update service; test with a fake in-memory provider and local fixture HTTP server.

### Required downloader behavior

- Certificate and hostname verification **always enabled**; TLS 1.2 or higher wherever available; normal OS/system trust store. Never use `CURLOPT_SSL_VERIFYPEER=0` or `CURLOPT_SSL_VERIFYHOST=0` as a workaround.
- Follow safe HTTPS redirects (GitHub uses them), while prohibiting redirection to `file:`, `ftp:`, or plain HTTP.
- Stream large files to `.part` files; do not buffer game archives in RAM.
- Download resume uses `Range` only after checking the local partial file's metadata. Validate `206` and `Content-Range`; if ignored or inconsistent, **restart cleanly** instead of appending. Verify full expected archive size and SHA-256 before considering it complete. Cached `ETag`/Last-Modified are hints, not replacements for SHA-256.
- Configurable connect/transfer/low-speed timeouts, bounded retries with backoff for transient errors, cancel token, proxy support through normal libcurl facilities, informative errors for 403/404/429/5xx, disk full, and certificate failures.
- Bound response size for signed manifest envelopes (e.g., 16 MiB), downloaded package size by manifest plus hard safety limits, ZIP extraction size, file count, depth, and path lengths.
- When offline, retain valid local installation and show nonblocking status; never wipe local state merely because an update check fails.

GitHub Release assets currently have a per-asset size limit below 2 GiB. The publisher must split the snapshot into **multiple independent ZIP packages** below a safe threshold (e.g., 1.5 GiB) by assigning files to ZIPs. If one game file alone exceeds that limit, fail publishing clearly until an explicitly designed multipart-file mechanism is available; do not generate invalid or silently truncated assets.

---

## 9. Trust model and signed manifest envelope

Use **Ed25519**, not a self-signed X.509 certificate. The publisher has a secret private key stored securely outside Git. Production launcher binaries contain only one or more explicitly trusted 32-byte public keys and key identifiers. Use Monocypher's **optional Ed25519 implementation** with its correct Ed25519-compatible verification API.

Expose exactly **one fixed URL** for the latest manifest, where the downloaded JSON file is a signed envelope:

```json
{
  "envelope_version": 1,
  "key_id": "release-key-2026-01",
  "payload_b64": "BASE64_OF_EXACT_UTF8_MANIFEST_JSON_BYTES",
  "signature_b64": "BASE64_OF_64_BYTE_ED25519_SIGNATURE"
}
```

`payload_b64` decodes to the **exact UTF-8 byte sequence** signed by the publisher. Verify the signature over those bytes **before parsing or trusting the payload**. This avoids two-file manifest/signature synchronization races and complex JSON canonicalization rules. The envelope fields are untrusted until the signature is accepted; `key_id` may only select from a fixed embedded allowlist. Reject unknown keys, malformed base64, wrong signature length, too-large envelopes, invalid UTF-8, duplicate or unknown critical manifest schema fields, and unsupported versions. Use the same exact payload bytes when computing a manifest-content hash.

Use SHA-256 to validate every downloaded ZIP and each installed managed file. Signature authentication and file integrity checks are different and both mandatory. No manifest or package may cause arbitrary shell command execution.

### Anti-downgrade rules

- Releases have a monotonically increasing unsigned integer `sequence` and human-friendly `version`.
- Persist the highest *authenticated and successfully accepted* release sequence and the hash of its signed manifest payload in durable per-installation state, and optionally per-user configuration across reinstallations.
- Never silently install a lower sequence than one previously accepted for that installation.
- Reject an unexpectedly different payload at the **same** release sequence, unless there is a separately designed migration/recovery flow.
- A newly installed launcher with no previous state cannot independently prove it has received the newest ever-published validly signed manifest; HTTPS plus secure publication mitigate this bootstrap freshness limitation. Document that limitation rather than claiming absolute replay prevention.
- `min_launcher_version` may indicate that an incompatible old launcher needs a manual new executable. **Do not** implement self-update in v1.

### Filesystem threat model

Treat ZIP entries, manifest paths, filenames, symlink/reparse behavior, lengths, sizes and archive counts as untrusted even when signed; publication mistakes happen. Reject absolute paths, `..`, empty components, drive letters/UNC prefixes, NUL, escaping Unicode variants where applicable, Windows reserved names and trailing-dot/space ambiguity, path aliases/case-fold collisions, duplicate entries, symlinks/hardlinks/device entries, and extraction through an existing symlink/reparse point. Canonicalize/validate destinations *within* the game root before writing, and avoid TOCTOU by checking paths at the point of use. Only regular managed files and explicitly allowed directory structures are installable.

---

## 10. Manifest format v1 — formal behavioral contract

The **signed payload** (not the outer envelope) is UTF-8 JSON. Names below are normative; extend them conservatively and document schema migrations.

### Illustrative shape (placeholder values, NOT a valid release)

```json
{
  "schema_version": 1,
  "release": {
    "version": "0.2.0",
    "sequence": 2,
    "created_utc": "2026-10-12T08:00:00Z"
  },
  "min_launcher_version": "0.1.0",
  "packages": [
    {
      "id": "win-base-2-a",
      "kind": "snapshot",
      "platform": "windows-x64",
      "url": "https://github.com/ORG/REPO/releases/download/v0.2.0/win-base-2-a.zip",
      "size": 12345678,
      "unpacked_size": 34567890,
      "sha256": "REPLACE_WITH_64_HEX_CHARACTERS",
      "files": [
        {"path": "bin/Game.exe", "size": 8000000, "sha256": "REPLACE_WITH_64_HEX_CHARACTERS"},
        {"path": "splash.bmp", "size": 300000, "sha256": "REPLACE_WITH_64_HEX_CHARACTERS"}
      ]
    },
    {
      "id": "win-1-to-2",
      "kind": "patch",
      "platform": "windows-x64",
      "url": "https://github.com/ORG/REPO/releases/download/v0.2.0/win-1-to-2.zip",
      "size": 1234567,
      "unpacked_size": 8000000,
      "sha256": "REPLACE_WITH_64_HEX_CHARACTERS",
      "files": [
        {"path": "bin/Game.exe", "size": 8000000, "sha256": "REPLACE_WITH_64_HEX_CHARACTERS"}
      ]
    },
    {
      "id": "lang-de-2",
      "kind": "language",
      "platform": "any",
      "url": "https://github.com/ORG/REPO/releases/download/v0.2.0/lang-de-2.zip",
      "size": 300000,
      "unpacked_size": 500000,
      "sha256": "REPLACE_WITH_64_HEX_CHARACTERS",
      "files": [
        {"path": "Data/texts-de.pak", "size": 500000, "sha256": "REPLACE_WITH_64_HEX_CHARACTERS"}
      ]
    }
  ],
  "platforms": {
    "windows-x64": {
      "launch": {
        "executable": "bin/Game.exe",
        "working_directory": "bin",
        "args": [],
        "environment": {}
      },
      "files": [
        {"path": "bin/Game.exe", "size": 8000000, "sha256": "REPLACE_WITH_64_HEX_CHARACTERS", "executable": true},
        {"path": "splash.bmp", "size": 300000, "sha256": "REPLACE_WITH_64_HEX_CHARACTERS", "executable": false}
      ],
      "snapshot_package_ids": ["win-base-2-a"],
      "managed_user_dir_files": [],
      "update_edges": [
        {"from_sequence": 1, "to_sequence": 2, "package_ids": ["win-1-to-2"]}
      ]
    },
    "linux-x64": {
      "launch": {
        "executable": "bin/Game",
        "working_directory": "bin",
        "args": [],
        "environment": {
          "DXVK_WSI_DRIVER": "SDL3",
          "LD_LIBRARY_PATH": "${INSTALL_DIR}/bin"
        }
      },
      "files": [],
      "snapshot_package_ids": [],
      "managed_user_dir_files": [],
      "update_edges": []
    }
  },
  "languages": {
    "en": {
      "label": "English",
      "path": "Data/texts-en.pak",
      "package_id": "lang-en-2",
      "size": 400000,
      "sha256": "REPLACE_WITH_64_HEX_CHARACTERS"
    },
    "de": {
      "label": "Deutsch",
      "path": "Data/texts-de.pak",
      "package_id": "lang-de-2",
      "size": 500000,
      "sha256": "REPLACE_WITH_64_HEX_CHARACTERS"
    }
  }
}
```

The example intentionally abbreviates the Linux section and the English package; the **publisher must refuse** inconsistent/incomplete manifests. It must generate all file/package records from input files, rather than copying these placeholders.

### Manifest invariants

- Valid semver-like visible `version`, strictly monotonically increasing integer `sequence`, documented `schema_version`.
- Exactly one target state per advertised platform, with **complete** managed base file list (`path`, length, SHA-256, executable mode). Platform IDs are explicit, initially `windows-x64` and `linux-x64`.
- Every snapshot set, when unpacked, reproduces the target's managed **base** files exactly (excluding language packs, user data, launcher metadata and executable).
- Language packs each contain exactly the language file(s) expressly declared for that language; v1 assumes one `Data/texts-<locale>.pak` file per locale.
- Every patch ZIP contains **complete replacement file bytes**, NOT binary deltas. Patch records list every archived file, its size and hash. Removed files are inferred from the difference between the previous managed set and the desired target state; deletions can only affect previously managed paths. Inferred deletes do not apply to any untracked paths.
- Each `update_edges` item describes a valid *contiguous* previous-sequence → next-sequence transition and lists the relevant patch packages. The signed latest manifest must contain the edge metadata required by any advertised supported direct/chain upgrade path; if a path is missing, the planner must use the latest snapshot.
- An immutable package record contains ID, kind, applicable platform, HTTPS URL, compressed byte size, total unpacked size, ZIP SHA-256, and exact file inventory with individual SHA-256 hashes.
- All IDs and normalized paths are unique; package file inventories, target files, language records, and platform memberships must be cross-validated. Conflicting overlaps and unknown critical fields are errors. A path under `MODs/` or `Profiles/` can be listed as managed **only if** its exact path is in that platform’s signed `managed_user_dir_files` allowlist; all others in those folders are user-owned and untouchable by automatic cleanup.
- A ZIP package must not contain unlisted payload files. Directories may be implicit and are not executables. Executable permissions for Linux must be restored from the **signed target file record**, not blindly inherited from ZIP metadata.
- A newer manifest may remove old update edges as long as the newest snapshot remains usable. Never rely on historical GitHub assets that have been deleted; publisher validation must check references.
- Store sizes in nonnegative 64-bit integers and use checked arithmetic for total-size calculations.

### Windows/Linux game launch profiles

The actual OpenBK2 repository has a Linux script running `bin/Game` **with `bin/` as working directory** and with `DXVK_WSI_DRIVER=SDL3` and `LD_LIBRARY_PATH` set to the installation's bin directory. Replicate essential launch behavior directly without requiring that shell script. Treat `DXVK_LOG_LEVEL=debug` from its current developer script as diagnostic, not necessarily as a production default. On Windows prefer `bin/Game.exe` and validate with the actual published game build. All executable paths and variable substitutions must be restricted to safe, manifest-declared values. No shell interpolation.

---

## 11. Update planner and package granularity

The first version supports **full current snapshots and complete-file ZIP patches**, not binary-delta patches.

### Clean installation

1. Fetch and authenticate latest manifest.
2. Choose the target platform and requested language.
3. Plan all ZIPs from the latest `snapshot_package_ids` plus **one** matching current language package.
4. Preflight disk space: cached ZIP data + extracted staging + backup/transaction margin + eventual installation footprint; show a meaningful low-space error.
5. Download all packages, verify package SHA-256 and declared length.
6. Safely extract into isolated staging within `.launcher/`; verify each staged file against its target SHA-256 and size.
7. Commit with journal/recovery mechanism, record local state, generate `launcher_installed.txt`, and enable Play.

### Existing installation update

1. Load local state and **recover any unfinished transaction first**.
2. Fetch/authenticate latest manifest. Refuse silent downgrade or same-sequence mutation.
3. Calculate desired managed paths for the platform plus chosen language; compare with local state and actual disk hashes. Full verification is allowed to take time but must not block the GUI; later optimizations may skip unchanged files only if they preserve correctness guarantees.
4. If no changes are required, mark Up to date. If a repairable managed file is modified or missing, include it in the plan and protect the modified version as needed.
5. If the local release sequence and verified files allow a contiguous patch chain to the target, compute the **sum of ZIP download sizes** and use that chain only if smaller than the snapshot and fully covers required final bytes; otherwise choose the latest snapshot. It is acceptable for v1 to use a simple chain-versus-snapshot comparison rather than a global optimal set-cover algorithm.
6. Fetch packages, validate, stage resulting **final target bytes**, and ensure all desired changed/new managed files are present and valid. Do not commit obsolete intermediate files from patch ZIPs. If a patch chain cannot construct the final target, **fall back** to the full snapshot before modifying the installation.
7. Detect files present in the previously tracked managed set but absent from the new target. Validate prior ownership against the **previously installed signed manifest**, not just editable state. Delete/replace **only those authenticated tracked paths**, protecting local modifications with backups. If prior ownership cannot be authenticated, preserve existing files and enter a safe adopt/repair reconciliation mode instead of guessing.
8. Commit and verify. Advance state to latest release only on success; generate readable install summary.

Support skipping any number of releases. The normal user-facing operation always installs the newest authenticated release; no version chooser, no historical rollback button. On corrupted state, missing old patch assets, incomplete patch lineage, or uncertainty, a full snapshot and repair is the safe fallback.

### Repair/full reinstall

Keep Repair and Full Reinstall implemented as planner modes even if the first public UI exposes them under Settings later. Repair compares all managed files and restores incorrect/missing ones; it must NOT delete untracked game files or personal data. Full reinstall restores the managed game state from the latest snapshots and current language while preserving untracked user-owned content by default, with explicit warning for conflicting modified managed files. Neither operation permits selecting old releases.

---

## 12. Transaction, crash recovery, and atomicity

A sequence of updates to many individual files cannot be globally atomic just by using `rename()`. Implement a **durable journaled transaction** so an interruption cannot be mistaken for a valid installed release.

Suggested update state machine:

```text
UNCONFIGURED
  -> READY_TO_INSTALL
  -> FETCHING_MANIFEST
  -> PLANNING
  -> DOWNLOADING
  -> VERIFYING_PACKAGES
  -> EXTRACTING_AND_VERIFYING_FILES
  -> PREPARED
  -> COMMITTING
  -> VERIFYING_INSTALL
  -> READY

Failures before PREPARED: safely clean/reuse partial downloads, leave existing game untouched.
Failure or crash after COMMITTING: on next startup enter RECOVERING before Play.
RECOVERING: finish the prepared commit if possible or restore the immediately
             preceding consistent local state from temporary backups, then retry.
```

Requirements:

- Keep staging on the installation filesystem and never extract directly into live `bin/`, `Data/`, or `Editor/` folders.
- Maintain `transaction.json` with a unique transaction ID, old/new release sequences, intended file operations, progress checkpoints, and backup paths. Update/flush the journal in an order that permits idempotent recovery. Platform-specific durability requires attention to fsync/FlushFileBuffers and rename/replacement semantics.
- Back up all tracked files that will be overwritten/deleted **before** they become unrecoverable. Use a per-transaction backup folder. Do not remove old copies until final verification succeeds and state has been durably written.
- Individually replace files through safe same-filesystem operations. On Windows, respect file locks and replacement API constraints; if a game executable or DLL is in use, abort cleanly before damage and ask users to close the game.
- On startup, a pending commit/recovery journal takes precedence over ordinary Play/Update. UI says `Recovering installation`, with real progress and actionable error on unrecoverable failure.
- Never mark a release installed until required hashes are verified, the corresponding **signed installed-manifest envelope** has been stored, and local state has been atomically/durably replaced. A deleted journal must not precede successful state and signed-manifest persistence.
- Cancellation before commit is safe; cancellation during commit either finishes a safe boundary or transitions into recoverable state. No partially updated game may be advertised as ready.
- Support fault-injection tests that simulate process interruption after every meaningful journal/write/rename step.
- Keep backups only as needed for transaction recovery and preserved user modifications; do not expose historical versions or retain full old builds indefinitely. Provide retention rules and disk usage limits that never delete the *only* recovery copy of an in-progress transaction.
- Use an OS-backed single-instance/updater lock; handle stale processes safely through OS lock release. A simple PID text file is not sufficient as the only lock.

---

## 13. Local state and human-readable status file

`OBK2/.launcher/state.json` is an internal **bookkeeping record**, not proof of file authenticity or path ownership on its own. Save and authenticate the previously installed signed envelope separately at `OBK2/.launcher/installed_manifest.envelope.json`, using it to validate the prior managed inventory before destructive changes. Example structure:

```json
{
  "state_schema_version": 1,
  "installed_release": {"version": "0.2.0", "sequence": 2, "signed_payload_sha256": "..."},
  "platform": "linux-x64",
  "installed_language": "de",
  "highest_accepted_sequence": 2,
  "managed_files": [
    {"path": "bin/Game", "size": 8000000, "sha256": "..."},
    {"path": "Data/texts-de.pak", "size": 500000, "sha256": "..."}
  ],
  "updated_utc": "2026-10-12T08:05:00Z"
}
```

The launcher may add a separately stored `requested_language` when a switch is pending. The UI should distinguish installed from requested language. Use atomic state writes (write temporary file, flush, replace) and explicit schema migration/error handling.

Create/update `OBK2/launcher_installed.txt` with a clear, non-authoritative summary such as launcher version, game release version, platform, installed language, and date. Do not use text-file marker names like `patch_1.1` as update truth; rely on verified managed file inventories.

Use structured error categories (`Network`, `Signature`, `Manifest`, `PackageIntegrity`, `Filesystem`, `DiskSpace`, `Permission`, `GameRunning`, `UnsupportedPlatform`, `Recovery`, etc.) and logs with a bounded retention policy. Never log access tokens or private keys. Provide explanatory UI errors, with a path to the detailed log when useful.

---

## 14. Launching and detecting the running game

Implement `GameLauncher` independent of UI. It must launch the **verified local executable** with a game-root-relative executable path, correct `bin/` working directory, manifest-declared arguments, and constrained environment variables. Use SDL3's process APIs (for example `SDL_CreateProcessWithProperties`) or safe native APIs. **Do not build shell command strings or invoke a shell.** Handle spaces and Unicode paths correctly.

For Linux, inspect and reproduce the essential behavior from OpenBK2's real `launch.sh`, notably `LD_LIBRARY_PATH=<install>/bin` and `DXVK_WSI_DRIVER=SDL3`, without relying on an external shell script. Ensure executable bit is restored. Do not blindly enable the developer script's debug flags in the production profile.

Track the child process that the launcher starts. Disable game updates while that process runs and present a useful message. Perform best-effort detection of manually launched game processes where practical, but **do not falsely claim** universal reliable detection without cooperation from the game; handle locked files safely. Play should work offline and should not require a new manifest fetch if an installation was previously verified and has no pending recovery.

---

## 15. Python-only publisher and release workflow

All first-party developer-side distribution/publishing scripts must be **Python**, cross-platform on Windows and Linux. Do not introduce `.cmd`, `.bat`, `.sh`, or PowerShell scripts for the publication pipeline. Python 3.11+ is fine; a declared dependency on `cryptography` for signing is fine. Runtime users never need Python.

Provide a coherent CLI, either a single `tools/release.py` with subcommands or a small group of Python modules, supporting at least:

```text
python tools/release.py keygen --output-private <secure-outside-repo-path>
python tools/release.py build --version <version> --sequence <n>
    --windows-root <built-windows-game-directory>
    --linux-root <built-linux-game-directory>
    --languages-dir <language-pack-inputs>
    --previous-release <optional-previous-release-metadata>
    --out <release-staging-directory>
python tools/release.py verify --release-dir <release-staging-directory>
python tools/release.py sign --release-dir <release-staging-directory>
    --private-key <path-outside-repo>
python tools/release.py publish-github --release-dir <dir>
    --owner <owner> --repo <repo> --tag <tag> --manifest-repo <owner/repo>
    --manifest-branch <branch>
```

Arguments may be adjusted to improve ergonomics but all core operations must exist. Signing may use `OPENBK2_SIGNING_KEY_FILE` or an explicit external file; never accept the secret as a command-line literal if that would expose it in process listings. GitHub publication uses a token from environment / trusted CI secrets and does not write it into logs.

### Build and package generation

- Scan supplied built game roots. Exclude `.launcher`, launcher artifacts, generated files, and language packs from base snapshots. Treat `Profiles/` and `MODs/` as user-owned by default; **if essential official game files live there**, require an explicit publisher-side exact-path allowlist and emit those paths in signed `managed_user_dir_files`. Never auto-include an entire user-owned directory. Allow explicit exclusions for known mutable/generated files elsewhere.
- Build newest complete platform snapshots, splitting at a safe package size limit; package only game-root-relative paths.
- If previous-release metadata/input is provided, compute changed or added files from SHA-256 comparisons and create one or more patch ZIPs containing complete replacements; detect removed tracked paths through manifest differences. Generate update edges and preserve usable earlier edges if supplied. Do not fabricate a chain when earlier edges or source metadata are missing.
- Build separate ZIP packages for each game language file. Ensure one active language can be installed without shipping all others.
- Hash each output ZIP and each contained file; accurately record lengths, inventories, permissions, and launch profiles. Verify the complete generated target and all package references locally.
- Produce **deterministic** artifacts as far as practical: sort files, use fixed archive metadata/timestamps, stable compression settings, stable JSON serialization, no runtime-generated random package names, and record tool version. Distinguish byte-for-byte reproducibility goals from cryptographic security requirements.
- Prefer content-addressed/unique release filenames. Never overwrite an existing immutable game asset URL.
- Generate the exact manifest JSON bytes, sign them with Ed25519, create the **single JSON signed envelope**, and verify the signature as part of the publisher's own `verify` command.

### Publishing order

1. Validate generated release locally, including signed envelope, snapshot reconstruction, patch-chain reconstruction, and language switching against fixtures.
2. Create/upload immutable ZIP assets to the intended GitHub release via HTTPS/GitHub REST API from Python, with credentials obtained from environment/CI secrets. A draft release may be used while uploading.
3. Publish the asset release and confirm public package URLs can be downloaded (including redirects); never declare a release live before dependencies are reachable.
4. Update the fixed GitHub Pages/manifest URL **last**, preferably as a single file replacement via the GitHub Contents API on the configured Pages source branch. Keep previous manifests stored in the publisher workspace for audits and tests, but the launcher sees only latest.
5. Re-fetch the final published envelope and validate the signature and asset references.
6. On any failure before step 4, abort without advancing the publicly visible latest manifest. On failure after step 4, report a blocking publication incident and attempt a safe forward fix without silently publishing an older sequence.

Document GitHub configuration, repository token scopes, private key management, dry-run/local-only commands, changing to a future R2 backend, and retention of old assets needed by still-advertised patch chains. Include a **dry run** that makes no network mutations.

---

## 16. Build system and CI

- CMake targets: launcher executable, `launcher_core` library, and unit/integration test targets; use CTest.
- Fetch vendored/pinned dependencies reproducibly. Make it clear which are statically linked and which are ordinary OS libraries.
- Windows build: choose compiler/toolchain flags and runtime linkage that work for contemporary Windows and make Windows 7 x64 support plausible; validate DLL imports and supported APIs. No separate VC++ runtime installer should be required for a release build if technically achievable with the chosen compiler configuration.
- Linux build: use a conservative target environment; audit runtime dependencies with `ldd` and symbol-version tooling; do not claim “fully static” when relying on glibc/X11/Wayland/driver libraries.
- Use an embedded application icon and hero image; no source-tree relative asset loads in release builds.
- Produce `dist/OpenBK2Launcher-Windows-x64.exe` and `dist/OpenBK2Launcher-Linux-x64` (or equivalent clear names), each independently runnable.
- CI should build both, run tests, print actual sizes, fail if an artifact is **over 15 MiB**, warn when **over 10 MiB**, and archive binaries, test reports, dependency reports, and third-party notices.
- Keep performance measurements: launch startup responsiveness, idle CPU consumption, peak launcher RAM, and streamed download memory use. Prefer optimization after behavior is correct, but never accept an accidental 50–100 MB launcher as “close enough.”
- Static library linking is a design goal, **not a reason to disable TLS security, omit error handling, or ship an untestable binary**.

---

## 17. Automated tests — non-negotiable acceptance suite

Create small synthetic game releases with directories such as `bin/`, `Data/`, `MODs/`, `Profiles/`, `Editor/`, and `splash.bmp`, with tiny dummy executables and data. Use local HTTPS/HTTP fixture transport under test-only configuration; release builds must still enforce production HTTPS and embedded production public keys.

### Happy paths

1. Fresh Windows fixture installation with English language results in exactly the expected signed managed files.
2. Fresh Linux fixture installation with German language preserves executable permissions and correct launch profile.
3. Update sequence 1 → 2 changes/adds/removes exactly the intended managed files.
4. Update sequence 1 → 3 using a valid multi-step patch chain and latest target, including an intermediate file updated twice.
5. Update sequence 1 → 3 where a patch edge is unavailable falls back to the current full snapshot.
6. Package planner chooses a smaller valid patch chain over full snapshot and chooses snapshot when chain is larger or invalid.
7. Switching `texts-en.pak` → `texts-de.pak` changes only managed language files, preserves non-language game files, and persists selection.
8. GUI displays actual download bytes and stage progress; Play works offline on an installed fixture without fetching a manifest.
9. Existing-game adoption recognizes valid files and preserves unknown files.
10. Repair restores a deliberately corrupted managed file without deleting untracked `MODs/`, `Profiles/`, or other user files, while handling specifically allowlisted official packaged files correctly.

### Security and corruption

11. Invalid Ed25519 signature, unknown key ID, changed signed payload, unsupported schema, and malformed envelope are all rejected before installation.
12. Corrupted ZIP digest, truncated ZIP, incorrect extracted file digest, and size mismatch are all rejected.
13. ZIP-slip `../`, absolute path, symlink, Windows reserved name, case-fold collision, duplicate archive entry, hard link/device entry and decompression-bomb-like limits are rejected.
14. Manifest referencing `.launcher`, launcher executables, or generated status paths is rejected; managed paths under `MODs/` or `Profiles/` require signed, exact-path `managed_user_dir_files` opt-in.
15. An older signed release sequence cannot replace a later accepted one; a different manifest at the same sequence is rejected.
16. HTTP redirect downgrade is blocked, TLS verification is mandatory, Range response inconsistencies trigger restart rather than bad append.
17. Reusing an existing unrelated filename does not silently delete/overwrite an untracked file; modified managed files are backed up before replacing them. Corrupt or forged `state.json` without a valid previous signed envelope cannot authorize deletion.

### Reliability, OS, and lifecycle

18. Interrupted downloads resume correctly when server honors Range, restart correctly when it does not, and verify the completed ZIP.
19. Network outage, 404, 429, 5xx, timeout and disk-full failures leave a previously good installation playable when no commit began.
20. Fault injection at **each** commit step, followed by launcher restart, results in successful recovery/complete current state or restoration of the previous consistent state. Never report a half-applied state as Ready.
21. Second concurrent launcher/updater instance cannot modify the same installation.
22. Language package download fails: previous installed language stays intact.
23. Low disk space before committing and a locked game executable yield actionable errors without corruption.
24. Unicode and spaces in selected installation path, Windows long-path limitations, Linux desktop shortcut escaping, and shortcut handling have platform tests.
25. Folder browsing callback threading, title-bar dragging, close/minimize buttons, worker progress and responsive UI all work.
26. Build and smoke test on contemporary Windows and Linux; run Windows 7 SP1 x64 VM test separately before reporting verified compatibility.
27. Assert the release executable is **≤ 15 MiB** and does not need separately shipped launcher libraries or asset files.
28. Test Python publisher's deterministic generation, validation of invalid input layouts, signing/verifying, patch chain construction, and mocked GitHub upload/publication-order failure behavior.

No automated test may require downloading the actual OpenBK2 game data or writing into a real user's game directory. Use deterministic fixtures and temporary folders.

---

## 18. Milestone implementation sequence

Implement in this order, showing test results after each stage:

**M1 — project bootstrap and renderer**  
CMake project, pinned dependencies, empty native SDL3+Dear ImGui app with custom borderless 1000 × 600 shell, working close/minimize/drag controls, embedded placeholder hero image, cross-platform smoke build. Establish preliminary size baseline.

**M2 — publisher fixture + manifest security**  
Python creates two/three tiny releases, zipped snapshots/patches and language packs, signs exact payload bytes, produces signed envelope. C++ core parses and verifies envelope, validates all schema/path invariants, and tests fail closed.

**M3 — downloader + staging**  
HTTPS downloader with safe redirect policy, cancellation, Range/partial support, SHA-256, ZIP extraction hardening, file-level verification, and event/progress reporting. Integrate against a test fixture server.

**M4 — planner + crash-safe installer**  
Clean install, incremental updates, patch-chain fallback, file ownership, language switching, local state, journal/backups/recovery and fault injection. Ensure this is completely tested without GUI.

**M5 — full UI and platform services**  
First-run wizard, settings, folder picker, language translations, shortcuts, relocation, main Play/Install/Update buttons, progress display and errors; execute the game's proper per-platform launch profile.

**M6 — publication + release hardening**  
GitHub Releases upload and latest signed envelope publication from Python, CI, binary-size enforcement, library audit, documentation, Windows 7 VM best-effort check and X11/Wayland testing. Only after fixtures pass, test with a small authorized subset of actual OpenBK2 game files and finally a complete game installation.

Do not substitute a README plan for actual implementation. If working within limited time/context, finish the current milestone with genuine tests and leave precise next steps rather than presenting placeholders as complete.

---

## 19. Deliverables and final developer report

The coding AI must deliver:

- A complete new source repository with all specified launcher modules, CMake files, embedded assets, licenses/notices, and build instructions.
- Fully functional release binaries for **Windows x64** and **Linux x64** wherever the execution environment supports building/testing them; if one target cannot be built in the environment, provide the complete build configuration and state honestly that its binary is **not yet verified**.
- Cross-platform **Python-only** publication tools, examples of commands, mock release artifacts, and a safe dry-run mode.
- Manifest schema documentation, signing/key generation instructions, update transaction/recovery documentation, platform support matrix, and a security notes document.
- A realistic checklist/results table for every acceptance test, identifying pass/fail/not-run and linking to logs where available.
- Actual Windows/Linux executable sizes, dynamic OS library dependencies, and outstanding compatibility limitations.
- A concise release-engineer checklist for signing, publishing, verifying, and announcing the latest game release.

### Final overriding principles

**One file to launch; one fixed signed manifest URL; latest desired game state; safe complete-file ZIP packages; one active managed language pack; no surprise deletion of user content; no launcher self-update; no background video; no Python on players' machines.**

**Never sacrifice authenticated downloads, user-data preservation, or recovery correctness to meet a cosmetic goal or a smaller binary. If necessary, remove optional niceties before weakening these guarantees.**

---

## 20. Upstream reference links for the coding AI

Verify API availability in the **pinned versions**, not in random third-party examples or obsolete posts. These are relevant starting points:

- OpenBK2 game: https://github.com/OpenBK2/OpenBK2 (current Linux launch script at https://github.com/OpenBK2/OpenBK2/blob/port/launch.sh)
- SDL3: https://wiki.libsdl.org/SDL3/README-windows ; https://wiki.libsdl.org/SDL3/SDL_ShowOpenFolderDialog ; https://wiki.libsdl.org/SDL3/SDL_SetWindowHitTest ; https://wiki.libsdl.org/SDL3/SDL_CreateProcessWithProperties
- Dear ImGui reference renderer: https://github.com/ocornut/imgui/tree/master/examples/example_sdl3_sdlrenderer3
- libcurl: https://curl.se/libcurl/ and https://curl.se/docs/install.html
- Monocypher optional Ed25519: https://github.com/LoupVaillant/monocypher
- Portable SHA-256: https://github.com/okdshin/PicoSHA2
- ZIP: https://github.com/richgel999/miniz
- GitHub release asset limits: https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases
- GitHub Pages usage constraints: https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits

Treat these links as references, not a requirement to adopt the newest incompatible API. Source-controlled dependency versions and working builds are the authority.
