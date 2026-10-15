# Launcher 0.3 initial data and startup verification

Implemented the maintainer's revised ownership and backup policy. Publishing,
manifest validation, seeding/reset/recovery and readiness checks remain in the
core and Python toolkit, independent of the GUI. Architectural changes are
recorded in [ADR 0003](ADR/0003-initial-data-startup-and-backups.md).

Launcher **0.3.1** also permits initial files under `Data/` alongside managed
assets, with full-path collision checks against both platforms and every
language. Reset replaces only declared initial Data files. Other Data content
is preserved; other initial folders retain whole-folder Reset behavior. See
[the input reference](PUBLISHING_INPUTS.md#initial-data) for layout and migration.

## Behavior and files

- `tools/release.py` / `tools/schema.py`: `--shared-init-data`, signed v3 initial
  inventories and shared ZIPs, separate from continuously managed game files.
  [Publishing inputs](PUBLISHING_INPUTS.md#initial-data) documents layout and migration.
- `manifest`, `local_state`, `update_planner`, `transaction`: initial-data scope,
  once-only markers, missing-file seeding, safe migration and crash recovery.
  Existing content and intentionally removed defaults survive ordinary updates.
- `initial_data.cpp`: complete-folder Reset with verified staging, confirmation
  through the controller/GUI, temporary originals, and offline commit/rollback.
- `controller`, `platform_services`, `application`: no full scan at startup or
  Play, brief background discovery, non-blocking automatic-check failures, and
  **Ready for playing**. Update/Repair still verifies managed file contents.
- `safe_files`, `installer`, `transaction`, `mod_manager`: retained backups are
  disabled by default. Temporary copies are still used for recovery and cleaned
  afterward. The backup implementation remains buildable and tested with
  `OBK_RETAIN_USER_BACKUPS=ON`; legacy retained backups are not purged.

The normal fixture artifacts in `dist/` use retention **OFF**. Rebuild the
production launcher with the existing URL/key before publishing v3; launcher
0.2 rejects the new schema. This work did not access or alter a real game install.

## Actual validation

| Check | Result |
|---|---|
| Windows 11, VS 2026/MSVC, MinSizeRel | Build succeeded; all 12 CTest suites passed in 112.63 s |
| Ubuntu 22.04 VM, GCC 11, MinSizeRel | Build succeeded; all 12 suites passed in 65.25 s |
| Retained backups explicitly enabled on Linux | Installer, initial data, mods and lifecycle suites: 4/4 passed in 43.72 s |
| Publisher CLI follow-up on Windows | Initial-data suite passed again with the actual `--shared-init-data` command |
| Legacy journal follow-up on Windows | Installer suite passed with explicit v1 journal/old backup-location recovery |
| Native X11 GUI | Install/render, folder picker, title drag, minimize/close, mod Download/Remove, and Settings Reset Cancel/Confirm passed |
| Native Wayland GUI | Signed fixture installation and rendering passed |
| Windows binary | 4,841,984 bytes (4.618 MiB), OS import allowlist passed |
| Linux binary | 9,597,488 bytes (9.153 MiB), OS dependency/glibc audit passed (maximum GLIBC 2.34) |

Both binaries meet the 10 MiB target. The new initial-data suite exercises 47
installation, 9 reset, and 6 rollback crash boundaries, plus adoption, v2-to-v3
migration, changed/deleted defaults, Repair/Reinstall preservation, reset scope,
missing/corrupt packages, cancellation, forged journals, conflicting paths,
hard links, and startup/Play with deliberately missing game assets. The existing
installer suite covers 52 additional update crash boundaries. Initial seeding
and reset test only synthetic fixtures; no complete game was downloaded.

One early Windows recovery run returned OS error 5 during rename. A preserved
diagnostic rerun, the full suite, and the legacy-journal follow-up all passed;
the failure was not reproduced. No verification or filesystem check was bypassed.
Windows directory-symlink creation was unavailable in the new fixture suite;
Linux exercised that case. Physical power loss, real-game startup/MP, network
filesystems, and Windows versions other than the local Windows 11 environment
were not tested. The existing Windows 7 import limitation remains documented in
[platform support](PLATFORMS.md).

Reports: [Windows suite](results/initial-data-windows-tests.log),
[Linux suite](results/initial-data-linux-tests.log),
[backups enabled](results/initial-data-backups-tests.log),
[publisher CLI](results/initial-data-cli-tests.log),
[legacy recovery](results/initial-data-legacy-tests.log),
[X11](results/initial-data-x11.json), [Wayland](results/initial-data-wayland.json),
[Windows audit](results/initial-data-windows-audit.json),
[Linux audit](results/initial-data-linux-audit.json).
