# Security model

Only an envelope signed by a compiled trusted Ed25519 public key can define
installable game state. The original payload bytes are verified before parsing;
JSON reserialization is never part of signature verification. Unknown keys,
unknown critical fields, duplicate properties, unsupported schemas and invalid
inventories fail closed. SHA-256 verifies both ZIP bytes and every managed file.

HTTPS certificate/hostname validation is mandatory, with TLS 1.2 minimum and
HTTPS-only redirects. Production has no certificate-bypass switch. Resumable
partials bind URL, signed length and digest; inconsistent Range responses restart.
Downloads, JSON, ZIP entries, expansion and archive directories have bounds.

Game files never write into launcher metadata or replace the launcher itself.
Traversal, drive/UNC paths, reserved names, aliases, duplicate entries and
links/special files are rejected. Handle-based operations pin directories and
check regular leaves; extraction uses new files in staging on the game volume.
Base-game ownership under `MODs/` and `Profiles/` requires exact signed file
opt-ins; all other entries are user-owned. Optional downloads use a signed v2
mod catalog scoped strictly to one `MODs/<id>/` folder, with no base-game overlap.
Archives and extracted bytes are verified before atomic folder publication.
Mods remain editable afterward, outside base-game integrity checks. Explicit
Remove actions move complete folders into recoverable backups; they do not
delete user files or grant an unsigned receipt authority over game data.
Modified and conflicting base originals are backed up before replacement.
Deletion requires an authenticated prior inventory, not merely editable state.

Installation paths must be absolute dedicated folders, not filesystem/drive/share
roots. Embedded NULs and ambiguous Windows device/alias paths are rejected before
game operations. Linux regular-file checks use nonblocking opens so a named pipe
cannot hang the check. The `.launcher` subtree is reserved working storage;
personal files do not belong in staging or cache directories.

An OS writer lock covers recovery, updates, mod operations and launcher relocation. A durable journal plus signed
inventories and verified backups supports repeatable commit or rollback. Play
requires complete signed local state, matching files and no pending transaction.
Cancellation is deferred during commit. Linux executable modes are restored;
game launch uses native argument vectors and a constrained signed environment.
Running-game detection is best effort for external processes. Hostile same-user
processes with arbitrary write access, malicious administrators, compromised
signing keys, hardware failure and a compromised operating system are outside
the security boundary. Store game data on a normal local NTFS/ext4 filesystem;
network/removable filesystem durability has not been validated.

The installed signed envelope anchors anti-downgrade and same-sequence mutation
checks. A fresh installation (or deliberate deletion of all local trust state)
cannot prove it has the newest historically signed release. HTTPS and publication
discipline mitigate bootstrap freshness; no absolute replay-prevention claim is
made. Key rotation and launcher replacement require a manually distributed new
launcher. There is no self-update or old-release selection mechanism.

Private signing keys belong outside this repository, protected by file permissions
or Windows ACLs. The publisher takes a key **file**, never a command-line secret.
GitHub tokens come from environment/CI secrets, are not logged, and are sent only
to allowlisted GitHub API hosts. Public asset verification sends no token. The
fixed manifest advances only after public package hashes pass; optimistic file
SHA checks prevent concurrent publishers from overwriting each other's manifest.

To report a vulnerability before a project contact is established, contact the
maintainer privately through the repository host's private security advisory
feature. Do not include private keys, authentication tokens or personal game data
in public reports. Keep the affected launcher version, OS, redacted logs, and a
minimal synthetic reproduction.

Relocation leaves an executable already in the chosen game folder in place.
A different destination executable is never overwritten; an active destination
launcher blocks relocation. Source cleanup requires a validated handoff, parent
exit and matching source bytes, and deletes only that source executable.

See the [filesystem safety review](docs/SECURITY_AUDIT.md) and
[acceptance results](docs/ACCEPTANCE.md) for measured coverage and limitations.
