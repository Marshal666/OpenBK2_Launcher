# Update and recovery protocol

The core is independent of SDL/ImGui. `Installer` holds an OS-backed writer lock
on `.launcher/update.lock` for planning, recovery and installation. Windows
write-sharing exclusion and Linux `flock(LOCK_EX|LOCK_NB)` release automatically
on process exit. A PID file does not grant ownership.

1. Recover an existing journal before ordinary planning or Play.
2. Authenticate the target envelope; authenticate the installed envelope before
   trusting local inventory. A malformed/missing state forces adoption without
   inferred deletion. A valid prior envelope still enforces replay protection.
3. Hash actual files. Calculate changed desired files and authenticated prior
   removals. Use a complete, smaller patch route only when prior base bytes and
   the simulated final inventory match. Repair/reinstall use snapshots.
4. Preflight ZIP, staging, apply-copy and backup space plus a 16 MiB margin.
   Temporary recovery storage is mandatory. Retained backups are disabled by
   default; when enabled, their 8 GiB quota stops changes before live mutation.
5. Download and validate archives, extract into per-package staging, copy only
   final target bytes into transaction staging, and discard intermediate files.
   A missing/invalid patch route falls back to the newest snapshot before commit.
6. Flush the target envelope, previous signed envelope/state and original file
   backups. Probe live Windows files for locks. Write/flush/rename the complete
   journal. Until its rename, no live managed file has been changed.
7. Copy each staged replacement to a flushed apply file. Move any live original
   to a recovery location, then rename the apply file into the now-empty path
   with no replacement allowed. New files appearing concurrently are preserved.
   Original copies and staged final bytes remain until verification succeeds.
8. Only after replacements, move authenticated obsolete files (including the
   previous language) out of the live target into recovery storage. Unknown
   paths and untracked language packs are never inferred as deletions.
9. Verify every desired file, apply signed executable modes, flush the installed
   envelope, state and readable summary, and record a transaction receipt.
   Remove the journal last. Only then may Play become ready.
10. Remove temporary copies under `.launcher/recovery/<transaction-id>/` and
    scratch files after the durable commit. With `OBK_RETAIN_USER_BACKUPS=ON`,
    the existing retention code instead keeps modified/untracked originals under
    `.launcher/backups/<transaction-id>/`. Cleanup failure produces a warning
    without invalidating committed state; later recovery retries temporary cleanup.

On restart, journal paths are validated against the saved signed target and
previous signed inventories. The journal alone cannot authorize a removal.
Recovery checks actual current hashes, not an editable progress index. It can
repeat any file step. If staged target bytes are lost, it records a rollback
phase and restores original bytes from verified temporary copies. Unexpected
live changes cause recovery to stop or are temporarily quarantined during rollback.
Neither outcome is reported as a complete new installation until verified.

Cancellation is honored during fetch/hash/extract/preparation. Once the journal
is durable the caller defers cancellation until a consistent boundary. Tests
use immediate process termination to exercise the other interruption path.

POSIX uses file/directory `fsync`, `openat` with `O_NOFOLLOW`, pinned directory
descriptors and Linux `renameat2(RENAME_NOREPLACE)`. Windows uses pinned directory
handles, reparse checks, `FlushFileBuffers`, and `MoveFileExW` with write-through.
Storage-device guarantees still depend on OS, filesystem and hardware. Process
crash tests are not claims of physical power-loss testing.

When no journal exists, recovery reclaims regular files under the reserved
`.launcher/staging` subtree, checking every directory and leaf for links first.
An existing journal blocks this reclamation. Download partials remain resumable.
Temporary recovery and discarded-mod trees are also reclaimed when no base
journal exists. Recursive cleanup is restricted to reserved launcher temporary
trees, rejects links and special files, and never deletes a game root. Legacy
retained backups are left alone. A v1 transaction journal retains its original
backup-location semantics so upgrades can still recover an in-flight operation.

## Initial data

Initial defaults are supplied by the signed v3 `initial_data` inventory.
Seeding only adds missing files in uninitialized folders and commits their
markers alongside the ordinary installed state. Subsequent updates, Repair and
reinstall neither verify nor reconcile those folders. Migration out of managed
files preserves the existing bytes. Startup and Play use authenticated metadata
and basic executable checks without hashing game assets.

Reset downloads and verifies the installed release's initial packages before
writing `.launcher/initial-reset.json`. The journal authorizes only the exact
signed folders. Originals are atomically moved into `.launcher/resets/<id>/original`,
then staged defaults are renamed into place without replacement. Every folder
must finish before the reset journal is removed. Recovery replays these moves;
if staged defaults are missing/corrupt, it rolls back all affected folders.
Temporary originals are deleted only after successful completion or rollback.
An enabled retained-backup build moves completed reset originals to
`.launcher/backups/reset-data/`. The UI confirms deletion of current settings,
saves and all additional files in the reset folders. Initial-data reset needs
no online update check and no network during recovery.

## Optional mod folder operations

`ModManager` holds the same installation writer lock and rejects an active game
process or pending base-game recovery. Download authenticates the catalog,
requires the installed game release to match, checks launch readiness, and checks
that no mod folder is present. It preflights download/staging space, downloads
only the selected mod's packages, verifies their full ZIP/file inventories, and
flushes the staged files with their signed executable modes. It rechecks that
the game is closed and `MODs` is empty before publishing the staged mod folder
with a single atomic, no-replace directory rename. Cancellation is honored until
that rename; partial downloads never appear as a live mod. Startup recovery
reclaims abandoned mod staging using the existing reserved-staging policy.

Remove validates the selected direct child of `MODs`, rejects base-owned folders,
links, hardlinks and unsafe paths, then moves the whole directory atomically to
`.launcher/discarded-mods/<unique-id>/<id>`, then cleans that reserved tree. The
single directory move is the commit point; a crash afterward leaves cleanup for
the next recovery. With retained backups enabled, the existing code instead
moves it to `.launcher/backups/removed-mods/<unique-id>/<id>`. Existing content
at a rename destination is never replaced. The UI confirms permanent deletion
under the default policy before invoking it. Base-game updates and
repairs leave optional mods untracked, preserving their edits and extra files.

The folder commit uses Linux `renameat2(RENAME_NOREPLACE)` plus parent-directory
`fsync`, or Windows `SetFileInformationByHandle(FileRenameInfo)` with replacement
disabled and pinned parents. A separate multi-file journal is unnecessary because
each operation has one atomic live change. Process-termination tests cover both
sides of publication and removal. Physical power-loss behavior has not been tested.
External programs that edit the installation without honoring the launcher lock
can still race an operation; unexpected destination folders cause a safe failure.

## Launcher relocation

The selected game folder is validated before relocation. If the executable is
already directly inside it, its current name and location are retained. Otherwise
relocation holds the same installation writer lock, copies only to an unoccupied
canonical launcher path or reuses a byte-identical existing copy, and refuses a
different file or an already-running destination launcher. A nonce identifies
the handoff record; source/destination identity, executable hashes and parent
process identity are checked before acknowledgement. Only after the parent exits
and the source still matches may that one source executable be removed. No source
directory is deleted. Failure to acknowledge keeps the original executable usable.
