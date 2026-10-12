# Developer handoff

OpenBK2 Launcher 0.1.0 is implemented as an independent C++20/CMake project with
an MIT-licensed launcher and Python-only release tools. Six milestone reports,
native binaries, synthetic fixtures, automated tests and build/audit evidence are
provided. The OpenBK2 game checkout and assets are not part of this project.

The working runtime verifies exact-byte Ed25519 envelopes, streams and hashes
packages, validates ZIP contents, selects complete-file patch chains or snapshots,
preserves user data, journals installs and recovers interrupted commits. The native
GUI supports setup/settings, languages, update/repair/reinstall, real progress,
offline Play, safe relocation and desktop shortcuts. The publisher generates,
validates, signs and uploads real releases, advancing the fixed signed envelope
only after public assets verify.

## Delivered files

* `dist/OpenBK2Launcher-Windows-x64.exe`: 3,258,368 bytes, tested on Windows 11.
* `dist/OpenBK2Launcher-Linux-x64`: 8,036,720 bytes, tested on Ubuntu 22.04,
  including X11 and Wayland renderers.
* `dist/SHA256SUMS.txt`: checksums for those exact fixture artifacts.
* `include/launcher` and `src`: reusable core and native application modules.
* `tools/release.py`: keygen/build/verify/sign/publish-github commands.
* `tools/run_fixture.py`: isolated interactive demo using the native test game.
* `tests/core`, `tests/integration`: security, publisher, transport, recovery,
  lifecycle, desktop and memory tests.

These executables are visibly marked **fixture/development builds**, per the
approved configuration. Production configure fails unless a real HTTPS manifest
endpoint, key identifier and Ed25519 public key are supplied. No private release
key is embedded or delivered. Python is not a player runtime dependency.

Start with the [README](../README.md) for native build commands and the one-command
fixture demo. Tests use temporary game roots; the demo persists under `build/demo`
and starts with relocation/desktop shortcuts disabled. Dependency sources, build
outputs and the user-approved portable Linux VM are workspace caches ignored by
Git, not required project source. The VM is shut down after verification.

## Evidence and release qualification

Windows and Linux each passed all nine CTest suites. Recovery was exercised at
52 process-crash boundaries on each OS. GitHub tests inject all 21 publication
boundaries and cover stale-CDN convergence/incident reporting. Binaries meet the
10 MiB target, with a build/CI-enforced hard maximum of 15 MiB. No companion
application libraries or external launcher assets are required.

* [Milestone reports and actual build/test results](MILESTONES.md)
* [Every acceptance case, with limitations and logs](ACCEPTANCE.md)
* [Platform matrix, exact hashes, imports and memory measurements](PLATFORMS.md)
* [Architecture and justified deviations](ARCHITECTURE.md)
* [Manifest contract](MANIFEST_V1.md) and [recovery protocol](UPDATE_PROTOCOL.md)
* [Security model](../SECURITY.md)
* [Release-engineer publishing checklist](PUBLISHING.md)

Remaining qualification is explicit: current Windows binaries have a Windows 7
runtime-import blocker; no Windows 7 VM test was performed. Actual GitHub
publication/hosted CI, physical power loss/full-disk tests and broad hardware
coverage have not been run. The user directed that authorized real-game subset
and full installation checks be marked **not run**. Those checks and production
trust configuration are required before claiming a qualified real-game release.

An empty Git repository was initialized during M1 before the user's later request
to handle Git themselves. No commits or remote were created; no subsequent Git
operations were performed. Repository setup/publication remains with the owner.
