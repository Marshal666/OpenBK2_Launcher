# Shared data and optional mods — launcher 0.2.0

This implements the user's extension to the original six milestones: a separate
shared input folder plus Windows/Linux inputs, and a publisher-signed mod catalog
with Download/Remove controls. Editing and enable/disable controls were explicitly
removed from scope. The initial six-milestone evidence remains in
[MILESTONES.md](MILESTONES.md); current usage is in [PUBLISHING.md](PUBLISHING.md).

## Implemented files and behavior

* `tools/release.py`, `tools/schema.py`: `--shared-root` and `--mods-dir`, shared
  snapshot/patch ZIPs, disjoint platform inventories, complete signed mod catalogs,
  v1-to-v2 migration and deduplication of repeated content-addressed patches.
  Existing `publish-github` uploads each package once and verifies all optional
  assets before advancing the fixed signed manifest.
* `include/launcher/manifest.hpp`, `src/manifest.cpp`, `src/update_planner.cpp`:
  strict v1/v2 verification, mixed shared/platform routes, mod scope/ownership
  checks, and single downloads for repeated package references across edges.
* `include/launcher/mod_manager.hpp`, `src/mod_manager.cpp`, `safe_files.*`:
  independent core operations using the installation lock, integrity checks,
  free-space checks, staging, atomic no-replace directory publication, and
  recoverable whole-folder removal. User edits and unknown mod files are retained.
* `controller.*`, `src/application.cpp`, `src/window_layout.cpp`: Mods catalog,
  Download/Remove controls, removal confirmation, progress/cancellation, one-mod
  rule, and game-running exclusion. Existing Settings/window buttons retain their
  positions. `src/platform_services.cpp` normalizes signed paths before comparing
  native Windows process image paths.
* `CMakeLists.txt`, `cmake/build_config.hpp.in`, `assets/windows.rc.in` and runtime
  version strings: version 0.2.0 and one new CTest suite. Tests add shared/mod
  synthetic fixtures, a native mod driver, HTTP/controller/process coverage and
  real X11 Mods clicks. No runtime dependency was added.
* `MANIFEST_V2.md`, `UPDATE_PROTOCOL.md`, `ARCHITECTURE.md`, `PUBLISHING.md`, README
  and the security model document the new schema and ownership rules.

## Actual verification

The native Windows 11/MSVC and Ubuntu 22.04/GCC builds each passed **10/10 CTest
suites**: 70.53 seconds on Windows and 40.02 seconds on Linux. The delivered
fixture binaries are **3,302,912 bytes (3.150 MiB)** on Windows and **8,073,648
bytes (7.700 MiB)** on Linux, both below the 10 MiB target and 15 MiB hard limit.
The new `shared_data_and_mods` suite performs **79 Windows / 81 Linux checks**;
Windows cannot create directory symlinks under this account, so those two new
checks run on Linux. Both platforms test hardlink refusal. Cases include:

* Shared archives referenced by both targets, correct platform executables,
  asymmetric v1 migration, multi-release shared patch reuse and snapshot fallback.
* Cross-language rejection of malformed signed catalogs, duplicate/overlapping
  paths, invalid package membership and base/mod ownership overlap.
* No mod downloads during game installation; selected mod installation across
  multiple archives; missing/truncated archives, cancellation during extraction,
  unknown IDs and premature use of a newer catalog.
* One-mod exclusion, update-lock exclusion, preservation through base repair,
  manual-folder removal, edited/extra-file backups, protected base folders, and
  process termination before/after both atomic folder moves.
* Offline GitHub transport verification that shared assets upload once and all
  mod assets are verified before manifest publication. No real GitHub upload.

`launcher_lifecycle` also exercises real loopback HTTP mod downloads through the
controller, native game launch, running-game refusal in controller/core, removal,
preserved player edits and download of a different mod. The synthetic game waits
for a test signal so process exclusion does not depend on a short sleep.

The native X11 test uses pointer clicks to Download Alpha and Remove it, then
checks the actual installed files and retained player edit. It also retains the
folder-picker, title-drag and window-button tests. Weston/Wayland installation
and rendering pass; Wayland pointer interactions remain untested.

Evidence: [Windows CTest](results/v02-windows-ctest.log),
[Linux CTest](results/v02-linux-ctest.log),
[Windows JUnit](results/v02-windows-tests.xml),
[Linux JUnit](results/v02-linux-tests.xml),
[X11](results/v02-x11-results.json), [Wayland](results/v02-wayland-results.json),
[Windows binary audit](results/v02-windows-binary-audit.json),
[Linux binary audit](results/v02-linux-binary-audit.json).
The [Windows detailed test log](results/v02-windows-test-details.log) and
[Linux detailed test log](results/v02-linux-test-details.log) record individual
suite output and check counts. [Windows](results/v02-windows-build.log) and
[Linux](results/v02-linux-build.log) build logs are retained too.

## Limits and deviations

Manifest v2 is a deliberate extension authorized by the user. It requires launcher
0.2.0; old launchers refuse it. The updated launcher still accepts v1. Mods use
one catalog with shared bytes for both platforms and must match the installed
game release. They stay user-editable after installation; game repair does not
enforce mod contents or automatically update them. Removal means backup, not
deletion. This preserves the original user-file guarantees.

Physical power loss, real GitHub publication/hosted CI, Windows 7 and broad hardware
coverage were not newly qualified. The existing Windows 7 runtime-import blocker
remains. Actual game-data installation and game/mod compatibility are **not run**
at the user's request. Production still requires a supplied HTTPS endpoint and
public key. No Git initialization, commits or remote operations were performed.
