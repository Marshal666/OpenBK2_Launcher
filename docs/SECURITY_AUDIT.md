# Filesystem and relocation safety review — 2026-10-14

This is the 0.2 behavior review. Launcher 0.3 deliberately changes pre-Play
scanning and retained backups; see [the updated decision](ADR/0003-initial-data-startup-and-backups.md)
and [current security model](../SECURITY.md).

This review covers installation/adoption, deletion and cleanup, missing local
metadata, launcher relocation and overlapping processes. It includes source review
and native tests on Windows 11 and Ubuntu 22.04 using disposable signed fixtures.
Real game files and real drive roots were not modified or used for destructive tests.

## Answers to the reported cases

| Case | Verified behavior |
|---|---|
| Extra personal files in `Data`, `Profiles` or optional mods | Updates, Repair and Full Reinstall preserve paths outside the authenticated base inventory. Conflicting or edited managed originals are backed up before replacement. |
| Selecting `C:/`, `/` or a Windows UNC share root | Now rejected before installation/recovery writes. Validation also rejects empty/relative roots, embedded NULs, Windows device namespaces and ambiguous trailing-dot/space components. Tests exercise this validation without opening those roots or shares. |
| Missing `.launcher/downloads` | The download cache is dispensable. Offline Play still works when the signed installed state and actual game files verify. |
| Launcher beside the game, but no user settings | Setup still proposes `~/Games/OpenBK2`; executable location does not silently select a different installation. Choose the existing game directory if it is elsewhere. |
| Missing the entire game `.launcher` directory | The existing folder is adopted using a verified release. Missing ownership evidence cannot authorize deletions. Unknown and obsolete files remain; conflicting bytes are backed up. Offline Play needs authenticated installed state first. |
| Installation folder is already the executable's folder | Relocation now returns the current executable path without renaming it, starting a replacement or deleting the source. This also works for a renamed executable and when another launcher file is present. |
| Another, byte-identical launcher file is at the relocation destination | It is reused through the checked handoff. Only the old source executable is removed after the original process exits and its hash still matches. Its containing folder and neighboring files are untouched. |
| A different launcher version or unrelated file occupies the destination | It is never overwritten. Relocation reports an error and retains the source. A preceding game install/update may already have completed successfully. |
| The destination launcher is already running | Relocation reports an error and retains both files. It does not stop the existing process. |
| Another writer holds the installation lock | Update, recovery, mod changes and now relocation refuse to proceed concurrently. Multiple launcher windows can still exist. |
| Replacement startup/handoff fails | The original launcher remains. A copied destination may remain for inspection/retry. Forged handoff records fail source/destination/hash/parent validation. |

## Findings corrected

1. **Broad installation targets were accepted.** Added a common installation-root
   validator at settings, controller, installer, transaction, mod and launch/relocation
   boundaries. `SafeRoot` also rejects embedded NULs before native APIs can interpret
   a truncated path. General-purpose `SafeRoot` remains usable for pinned ancestors;
   installation policy applies at the game-operation boundary.
2. **Already-colocated launchers could be unnecessarily renamed and handed off.**
   Relocation now detects the same parent directory regardless of executable name
   and leaves it in place, including when a different canonical launcher file exists.
3. **Relocation lacked the shared writer lock and an active-destination check.**
   It now holds `.launcher/update.lock` through preparation and acknowledgement,
   and checks an existing destination process before starting another copy.
4. **Linux FIFO reads could hang before the regular-file check.** Reproduced the
   previous binary blocking during staging cleanup with a disposable named pipe;
   the test terminated that process after three seconds. Safe opens now use
   `O_NONBLOCK` so `fstat` can reject FIFOs immediately. Regular-file I/O retains
   its behavior. The new regression verifies rejection without deletion.

No code path was found that recursively deletes a selected game folder or drive.
`SafeRoot::remove` deletes an individual validated regular file, and game-file
removals require the previously signed inventory. A journal or editable local
state alone does not authorize deletion. File/directory conflicts and linked
entries fail rather than being recursively removed to make room.

## What can intentionally be removed

- Official files removed from a new signed release may leave the live game tree;
  modified originals are preserved in `.launcher/backups`.
- `.launcher` is reserved launcher storage. Regular files in its `staging` subtree
  are reclaimed when no recovery journal exists. Cache files and metadata can also
  be replaced. Do not put personal files there or remove recovery metadata during
  an interrupted update. Preserved backups are not automatically purged.
- Successful relocation may remove exactly the original launcher executable after
  the checked handoff and parent exit. No containing directory is removed.
- Explicit mod removal moves the complete folder into
  `.launcher/backups/removed-mods`, including local edits and extra files.

## Tests and evidence

- [Windows filesystem scenarios](results/safety-audit-windows-filesystem.json):
  9 cases passed, including both in-folder names, same/different existing targets,
  active destination, writer-lock contention, failed/forged handoffs and unsafe
  saved roots. [Linux scenarios](results/safety-audit-linux-filesystem.json): the
  same cases plus the FIFO regression passed.
- [Linux full CTest run](results/safety-audit-linux-ctest.log): **11/11 passed**,
  42.36 seconds. This includes signed-manifest/ZIP/TLS checks, forged state/journal
  rejection, user-file preservation, interrupted-write recovery, mods and lifecycle.
- [Windows full run](results/safety-audit-windows-ctest.log): nine tests passed;
  two needed follow-up. A new UNC assertion compared native spellings instead of
  normalized path text and was corrected. The sandbox's Schannel call returned
  `SEC_E_NO_CREDENTIALS`. The [targeted rerun](results/safety-audit-windows-recheck.log)
  passed both tests outside that sandbox with certificate verification unchanged.
  All **11 tests passed across those runs**. The final security test also passed
  again on Linux after the test-only adjustment.
- Added missing-all-metadata adoption coverage to `installer_recovery.py` and
  explicitly checked that offline lifecycle tests run without a download cache.
- Both rebuilt fixture executables remain under 10 MiB:
  [Windows audit](results/safety-audit-windows-binary-audit.json),
  [Linux audit](results/safety-audit-linux-binary-audit.json).

## Remaining limits

This establishes the tested behaviors, not a guarantee against every possible
data-loss bug. Physical power loss, failing hardware, network/removable filesystem
durability, privileged attackers and hostile processes modifying the same paths
remain outside this validation. External process detection is best effort; close
the game before changing its files. Signing-key compromise remains outside the
trust model. Real-game checks remain unrun at the user's instruction.

Root rejection covers filesystem namespace roots and Windows drive/share roots.
It does not classify every ordinary directory or Linux mount point as a safe game
location. Use a dedicated subfolder. Multiple GUI instances are allowed; the
writer lock serializes installation operations, and simultaneous preference saves
can still result in the last saved settings winning. The launcher does not
automatically upgrade an older launcher executable occupying the destination.
