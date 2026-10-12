# Milestone evidence

## M1 — 2026-10-12

Implemented: independent Git repository, C++20/CMake targets, static SDL 3.2.28
and ImGui 1.92.5 (SDL3/SDL_Renderer3), original embedded raster hero and window
icon, borderless 1000×600 shell, native hit test, close/minimize actions, keyboard
navigation, display scaling, and render smoke/size/layout tests. Production
configuration requires owner-supplied trust information. CI configuration covers
Windows 2022 and Ubuntu 22.04; it has not been executed remotely.

Files: root CMake files; `cmake/`; `include/launcher/{application,window_layout}.hpp`;
`src/{main,application,window_layout}.cpp`; `assets/hero.hpp`; `tests/core/`;
`tests/integration/windows_shell.py`; workflow, README, license notices and ADR 0001.

Actual Windows 11 x64 build: MSVC 19.51, CMake 4.3.1, MinSizeRel, static CRT.
Size **2,052,608 bytes (1.958 MiB)**. CTest **3/3 passed**: render three frames
and process quit; hard size limit; eight title-bar/scaling assertions. Missing
production values were rejected at configure time. Build and test logs are in
`build/m1-build.log`, `build/m1-tests.xml`, and
`build/windows/Testing/Temporary/LastTest.log`.

Native Windows renderer produced `build/m1-native/native-window.bmp`, visually
inspected. Native WM_NCHITTEST returned caption for the blank title bar and
client for the control region. The desktop test's actual button clicks could
not be executed: SetCursorPos is denied in this execution desktop, including
outside the sandbox. Minimize/close mouse behavior remains unverified; the test
fails explicitly rather than masking this as a pass. No claim of manual dragging
or high-DPI multi-monitor validation is made.

`dumpbin /dependents` reports KERNEL32, USER32, GDI32, WINMM, IMM32, ole32,
VERSION, ADVAPI32, SETUPAPI and SHELL32 only. No companion SDL/ImGui or VC runtime
DLL. Report: `build/m1-dependencies.txt`. Binary: `dist/OpenBK2Launcher-Windows-x64.exe`.

Linux build/runtime, X11/Wayland, and Windows 7 VM tests are not run: no Linux
environment or VM is installed. Native icon resource and full Unicode font
coverage are deferred to M5. Installation/update functionality starts at M2;
M1 explicitly identifies itself as a renderer milestone, without inert action
buttons pretending to install a game.

## M2 — 2026-10-12

Implemented: Python deterministic snapshot/patch/language packaging, splitting,
source-layout validation, exact-byte Ed25519 signing/verification, external key
generation, three synthetic releases, inherited patch lineage validation, C++
strict envelope/schema/path/inventory verification, streaming SHA-256 and
sequence checks. Core security has no SDL/ImGui dependency.

Files: `tools/{release,signing,schema,fixture_builder}.py`, pinned requirements;
`include/launcher/{error,manifest,sha256}.hpp`; `src/{manifest,signature,sha256}.cpp`
and JSON helper; new core/security and publisher integration tests; dependency
pins, notices, schema/publishing documentation and ADR 0002.

Actual Windows build succeeded without first-party compiler warnings. CTest
**5/5 passed**, including **38 security assertions** and **54 cross-language
publisher/manifest checks**. Tests use actual Python cryptography Ed25519
signatures checked by Monocypher, corrupted envelopes, unsupported schema,
unknown/duplicate fields, reserved/traversal/user paths, SHA sizes, references,
sequence rollback/mutation, ZIP truncation and byte-for-byte repeat builds.
Results: `docs/results/m2-tests.xml`; build log: `build/m2-build.log`.
Generated inspectable examples: `.fixtures/m2/release-{1,2,3}`. No private
fixture key is stored. These are data fixtures, not executable game programs.

The renderer binary remains 2,052,608 bytes: GUI integration of the new core
arrives in M5, so the linker currently omits unused security objects. This is
not a size measurement of the final launcher. Linux remains unbuilt/unverified.
Durable replay checkpoints, HTTPS transport, hardened extraction, transactional
installation and network publication are not claimed by M2. ASCII-only archive
member paths (Unicode installation roots remain allowed) are documented as a
conservative schema restriction in ADR 0002.

## M3 — 2026-10-12

Implemented: static libcurl with Schannel, HTTPS verification and redirect
protocol restrictions, bounded envelope fetch, streamed package downloads,
digest-bound partial metadata, validated Content-Range resume, clean restart,
bounded retry/backoff, cancellation and actual byte/speed callbacks. miniz
extraction verifies whole-package and per-file digests, preflights ZIP directory
size before allocating, checks names, duplicate/case aliases, entry types,
local/central header consistency, extra fields, expanded sizes and exact inventory.
Output is restricted to separately supplied staging roots and newly created files.
Filesystem primitives pin directory handles, reject links/reparse points and
hardlinked leaves, and provide flushed writes and same-root renames.

Files: downloader/release-provider/package-reader/safe-files headers;
`src/{http_downloader,zip_package_reader,safe_files}.cpp`; extended SHA-256;
transfer test driver and Python loopback HTTP/HTTPS/ZIP suite; dependency pins,
licenses, and test reports.

Actual Windows build succeeded. **6/6 CTest tests passed**; report at
`docs/results/m3-tests.xml`. HTTPS tests required execution outside the sandbox:
inside it, Schannel returns SEC_E_NO_CREDENTIALS. No certificate verification
option was relaxed. Tests cover successful trusted TLS, rejection of unknown CA,
HTTPS-to-HTTP and file redirects, downloads, progress bytes, interrupted/resumed
downloads, ignored/inconsistent Range, 404/429/503 retry policy, cancellation,
ZIP traversal/reserved paths, duplicate/case aliases, Unix link extensions,
symlink/device/FIFO/socket metadata, digest/size/truncation and existing-file
protection. The native transfer driver's DLL imports are archived separately.

Not yet tested: Linux filesystem behavior, disk-full/locked live game files,
power-loss durability and transaction recovery (M4), or GUI download integration
(M5). Windows junction races and live symlink-race stress are not claimed as
tested. ZIP64 *end directories* and packages over 60,000 entries are deliberately
rejected; publisher splits before this count. ZIP64 local size fields remain
supported. Whole game downloads were not used.

## M4 — 2026-10-12

Implemented: actual install/update/repair/reinstall/adoption and language switch;
verified prior ownership; costed patch-chain planning and snapshot fallback;
durable state/envelope/status writes; per-installation OS lock; staged commit,
backup preservation, quota/free-space checks, recovery and internal rollback.
New files never replace an existing live path without preserving its previous
contents. Recovery validates journal operations against signed inventories.

Files: local-state, planner, installer and transaction headers/implementations;
expanded safe-file operations; native installer test driver and Python recovery
suite; `docs/UPDATE_PROTOCOL.md` and archived results.

Actual Windows build succeeded and **7/7 CTest tests passed** in 36.57 seconds.
The installer suite covers fresh Windows/Linux-layout fixtures, 1→2 and 1→3,
twice-updated intermediate bytes, small/large/incomplete patch plans, missing
patch fallback, language success/failure, repair and full reinstall, adoption,
user/official-file backup preservation, forged state and journal, missing old
envelope, rollback/same-sequence rejection, Unicode and spaces, concurrent
updater exclusion, locked Windows executable, backup quota and injected
disk-full errors. A separate process exits at every instrumented commit boundary
and a new process recovers; missing staged bytes trigger restoration of old files.
Exact test counts and timings are in `docs/results/m4-tests.xml`.

Process-interruption recovery is tested on Windows/NTFS; physical power loss,
actual exhausted storage and Linux executable modes/runtime are not claimed as
verified. Linux-layout byte installation on Windows is not a native Linux test.
Long-path handling beyond the exercised Unicode/space paths still needs platform
coverage. Orphan scratch reclamation is documented as outstanding, with safe
space/quota failure rather than speculative deletion. GUI integration is M5.

## M5 — 2026-10-12

Implemented: worker-based controller, first-run/settings overlay, native asynchronous
folder picker, authenticated language selection, pending/installed language display,
Play/Install/Update/Repair/Reinstall, cancellation, real byte/stage progress, embedded
Roboto font, English UI with explicitly partial German translations, persistent
per-user preferences and bounded logs. Native game processes use verified signed
profiles without a shell. Relocation uses verified copy, nonce handoff, parent exit
and best-effort cleanup. Shortcuts use IShellLinkW or escaped desktop entries.

Files: application/main rewritten; controller, platform_services and settings modules;
build configuration header; native synthetic game/lifecycle driver and Python lifecycle
suite; font license and CMake wiring.

Actual Windows MinSizeRel build: **3,248,640 bytes (3.10 MiB)**. **8/8 CTest tests
passed in 42.57 seconds**, including the 197-check installer suite and native lifecycle
suite. Tested real child processes, Unicode/space paths, quoted arguments, correct cwd,
offline Play after failed update check, running-game update exclusion, language
persistence, native shortcut creation, successful relocation cleanup and destination
collision refusal. The real renderer/controller installed from a throttled loopback
HTTP server; captured frames show actual downloading progress and Ready/Play state.
Screenshots are in `build/lifecycle-artifacts/`; test results in `docs/results/m5-tests.xml`.

Not verified: physical folder-picker/mouse interaction on this locked desktop, compositor
drag/minimize behavior, Linux native launch/shortcuts/permissions, Windows 7. Only English
has complete UI coverage; German labels fall back to English. Embedded font coverage is
Latin/Greek/Cyrillic; other script glyphs need a future licensed font extension. No real
game data was downloaded. Linux-layout tests on Windows do not imply Linux support.

## M6 — 2026-10-12

Implemented: real Python GitHub REST uploads with restricted credential hosts,
immutable asset reuse, public streamed verification and optimistic fixed-envelope
publication last. Dry-run requires no token or network. Post-publication uncertainty
is reported as a blocking incident; authenticated stale CDN responses receive bounded
verification retries. Added CI builds/audits, embedded Windows icon/version resource,
native Linux X11/Wayland builds, orphan staging reclamation, executable-mode preservation
on recovery, explicit Windows path limits, Python/native ZIP-limit alignment,
native display tests and performance measurements. First-party C++ was formatted.

Files added/changed: `tools/github_publish.py`, `tools/release.py`,
`tools/{audit_binary,run_fixture,create_icon}.py`, `tests/integration/` publisher,
transport, lifecycle, display and performance suites; `src/{safe_files,transaction,
local_state,platform_services}.cpp`; CMake dependency/backend checks, icon resource,
workflow and license files; README, SECURITY, architecture/publishing/platform and
acceptance documentation, plus archived reports under `docs/results`.

Final native artifacts:

* Windows 11 / MSVC 19.51: **3,258,368 bytes (3.107 MiB)**.
* Ubuntu 22.04 / GCC 11.4: **8,036,720 bytes (7.664 MiB)**.

Both meet the 10 MiB target and enforced 15 MiB hard maximum. Windows uses static
CRT and ordinary system DLLs. Linux's direct dependencies are standard runtime
libraries, with maximum GLIBC symbol 2.34 and system graphical components loaded
as needed. Exact hashes/imports are in [PLATFORMS.md](PLATFORMS.md).

Actual final full CTest results: **Windows 9/9 in 63.57 seconds; Linux 9/9 in
40.52 seconds**. Includes 38 security assertions, 54 cross-language manifest/publisher
checks, 37 real transport/ZIP tests, 198 Windows / 196 Linux installer checks with
**52 process-crash boundaries on each OS**, and all **21 publication failure boundaries**.
The later publication-incident/CDN adjustment passed its targeted CTest on both OSes.
No application binary changed after the final full run.

The final Linux binary passed X11 native picker callback, pointer title drag,
minimize/restore/close and rendered installation, plus a Wayland rendered installation.
Native Windows rendering/child processes/COM shortcuts/relocation/offline Play pass;
physical Windows pointer controls remain unverified. The production branch compiled
with synthetic trust inputs for a compile-only check, passed smoke rendering and
rejected fixture flags. Dist artifacts were restored to fixture configuration.

Windows GUI measurements: visible window 0.072 seconds, 1.25% of one core idle,
62.05 MiB peak working set on a tiny fixture. Native streamed 64 MiB ZIP download/hash
peaks: **6.719 MiB Windows, 7.426 MiB Linux**, excluding GUI and Python server.

Outstanding release qualification: **Windows 7 is currently incompatible** with
the modern runtime synchronization import; no Windows 7 VM run is claimed.
Wayland pointer interactions, physical power loss, actual disk exhaustion,
live path-race stress, varied GPUs/high-DPI desktops and hosted CI/public GitHub
publication remain untested. Real-game subset and complete-game checks are
**not run at the user's explicit instruction**. Production URL/public key remain
owner inputs, as agreed. See the complete [28-case acceptance table](ACCEPTANCE.md).
