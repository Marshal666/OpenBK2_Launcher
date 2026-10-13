# Acceptance evidence

The user-requested 0.2.0 shared-data/mod extension has its own current
[build and test report](SHARED_DATA_AND_MODS.md). The M6 evidence below records
the original 0.1.0 acceptance run and remains available for comparison.

Recorded 2026-10-12 against the specification's 28 cases. Tests use isolated,
synthetic signed releases and native executable fixtures. **Pass** means the
described automated scope passed; it does not certify all hardware or hostile
concurrent filesystem behavior. **Partial** identifies a platform or physical
test still missing. No real user game directory is used.

Native CTest: **9/9 Windows and 9/9 Linux**. Evidence: [Windows JUnit](results/m6-windows-tests.xml),
[Linux JUnit](results/m6-linux-tests.xml), [Windows full log](results/m6-windows-ctest.log),
[Linux full log](results/m6-linux-ctest.log). The suites include 38 security
assertions, 54 cross-language publisher/manifest checks, 37 transport/ZIP checks,
198 Windows installer checks with 52 process-crash boundaries, native lifecycle
tests, and all 21 injected publication failure boundaries. Linux's installer
count is 196 because Windows file-lock probing is platform-specific. After the
full runs, the publisher incident/CDN refinements passed a further targeted CTest
on [Windows](results/m6-publisher-windows-tests.xml) and
[Linux](results/m6-publisher-linux-tests.xml); launcher binaries did not change.

| # | Result | Tested behavior and evidence |
|---|---|---|
| 1 | Pass | Fresh Windows/English target, exact signed managed inventory (`installer_recovery`). |
| 2 | Pass | Native Linux/German installation, executable mode, cwd and `LD_LIBRARY_PATH`/`DXVK_WSI_DRIVER` (`installer_recovery`, `launcher_lifecycle`). |
| 3 | Pass | 1 to 2 managed additions, replacements and removals; unknown content preserved. |
| 4 | Pass | 1 to 3 multi-edge route, including a file changed twice; target bytes verified. |
| 5 | Pass | Missing patch download triggers verified latest-snapshot fallback. |
| 6 | Pass | Smaller valid chain selected; larger and incomplete chains rejected for snapshot. |
| 7 | Pass | Language-only switch, unchanged base digest, removed old managed language, persisted selection. |
| 8 | Pass | Real throttled HTTP GUI download and frame capture; native offline Play after failed manifest check (`launcher_lifecycle`). |
| 9 | Pass | Existing fixture adoption, collision backup, unknown user files preserved. |
| 10 | Pass | Corrupt managed files repaired; official allowlisted MODs modifications backed up; unknown MODs/Profiles/Data survive. |
| 11 | Pass | Bad signature/key/payload/schema/envelope and duplicate JSON rejected by native verifier (`publisher_and_manifest`, `security_primitives`). |
| 12 | Pass | Wrong downloaded archive digest, truncated ZIP, wrong extracted digest and file length rejected (`transfer_and_staging`). |
| 13 | Pass | Traversal, absolute/drive paths, links/devices/FIFO/socket, reserved names, case aliases, duplicates and hardlink extras rejected. Signed ZIP claiming a 2 GiB entry rejected before staged payload writes. Existing filesystem hardlink target preserved. |
| 14 | Pass | Reserved launcher paths rejected; exact signed MODs/Profiles opt-in required. |
| 15 | Pass | Lower signed sequence and mutated payload at accepted sequence rejected. |
| 16 | Pass | Untrusted TLS certificate rejected, trusted test CA accepted, HTTPS-to-HTTP and file redirects rejected; inconsistent Range restarts. Native Schannel/OpenSSL verification stays enabled. |
| 17 | Pass | Unknown collisions and modified managed files preserved; forged state/missing old signed envelope cannot authorize deletion. |
| 18 | Pass | Cancellation retains partial; honored Range resumes; ignored/bad Range restarts; disconnected transfer retries and verifies final digest. |
| 19 | Pass (injected storage failure) | Real connection refusal, 404, 429, 503 and timeout exercised in transport. Installer package failures and injected disk-full errors retain a verified previous installation; offline native Play tested separately. Physical disk exhaustion not performed. |
| 20 | Pass (process crash) | Process exits at each of 52 instrumented commit boundaries; a new process recovers. Missing stage bytes force restoration. Physical power loss not performed. |
| 21 | Pass | Second updater denied by actual OS lock held by another process. |
| 22 | Pass | Failed language package leaves old language and verified installation intact. |
| 23 | Pass (injected space) | Zero available-space injection and real Windows locked executable stop safely with actionable errors. No physical disk-full volume used. |
| 24 | Pass | Native Unicode/space paths, explicit Windows directory-length rejection, Windows COM shortcut creation, Linux executable desktop entry parsed/launched by real `gio` with quotes/dollar/percent/backtick/backslash paths. Desktop trust prompts vary by shell. |
| 25 | Partial | X11 real pointer input exercises native picker callback, title drag, minimize/restore and close ([report](results/x11-results.json)). Windows native rendering/hit tests and worker progress pass; physical Windows clicks unavailable. Wayland rendered install passes ([report](results/wayland-results.json)); Wayland pointer/compositor interactions and high-DPI multi-monitor testing not run. |
| 26 | Partial | Windows 11 and Ubuntu 22.04 native builds and smoke tests pass. X11 and Wayland rendered installs pass. Windows 7 VM not run and current artifact has a known incompatible import; see [platform matrix](PLATFORMS.md). |
| 27 | Pass | Windows 3,258,368 bytes; Linux 8,036,720 bytes. Below both 10 MiB target and 15 MiB hard limit. System-library-only import audits, embedded assets, no companion application libraries. |
| 28 | Pass | Deterministic packaging, invalid source layouts, signatures, reconstruction/patch history and no-network dry-run. Mock GitHub tests cover immutable assets, public verification before manifest advance, replay guards, credential-host isolation and all 21 failure points. No actual publication. |

Production default configuration without trust inputs fails. A Linux production
branch built using explicitly synthetic compile-test trust inputs, rendered a
smoke frame and rejected a fixture-only flag with exit 2. Those inputs were not
installed into `dist`; shipped artifacts are visibly identified fixture builds.
[Production branch result](results/production-branch.txt).

Additional qualification status:

| Check | Status |
|---|---|
| Small authorized real-game subset | **Not run, at user's instruction.** |
| Complete OpenBK2 game install/update/launch | **Not run, at user's instruction.** |
| Live GitHub release/Pages publication | Not run; no account/repository credentials used. |
| Hosted GitHub Actions execution | Not run; workflow supplied and local equivalent builds/tests exercised. |
| Windows 7 SP1 VM | Not run; known import blocker in delivered Windows artifact. |
| Physical power-loss/disk-exhaustion tests | Not run; process termination and explicit storage-failure injection tested. |
| Concurrent malicious symlink/junction race stress | Not run; link/path validation and hardlink preservation tested. |
| Physical GPUs, multiple monitors and varied desktop shells | Not run beyond this Windows desktop and virtual Linux displays. |

Measurements and dependency details are in [platform evidence](PLATFORMS.md).
Historical milestone results are retained in [MILESTONES.md](MILESTONES.md).
