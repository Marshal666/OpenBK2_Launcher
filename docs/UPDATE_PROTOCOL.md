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
   Preserved backups default to an 8 GiB quota. Exceeding it stops before any
   live change; the launcher never discards a user's backup to make room.
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
10. Remove normal old copies and known scratch files. Modified/untracked copies
    remain under `.launcher/backups/<transaction-id>/` and are reported to the
    caller. Cleanup failure produces a warning without invalidating committed
    state. Empty scratch directories and small audit receipts can remain.

On restart, journal paths are validated against the saved signed target and
previous signed inventories. The journal alone cannot authorize a removal.
Recovery checks actual current hashes, not an editable progress index. It can
repeat any file step. If staged target bytes are lost, it records a rollback
phase and restores original bytes from verified backups. User changes noticed
after interruption are preserved or cause recovery to stop with an error.
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
Empty directories can remain. Backups from interrupted preparation are retained
conservatively because they can contain user changes; the 8 GiB quota and
free-space checks stop further writes if manual archiving is needed. No game
directory or preserved user backup is recursively deleted.
