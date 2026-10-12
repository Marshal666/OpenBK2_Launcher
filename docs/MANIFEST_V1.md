# Signed manifest v1

The normative field names and shape are in specification sections 9–10. The
Python producer (`tools/schema.py`) and C++ verifier (`src/manifest.cpp`) enforce
the same inventory contract. `tools/fixture_builder.py --out <empty-dir>` creates
three complete examples, with ephemeral signing keys held only in memory.

The envelope has exactly `envelope_version`, `key_id`, `payload_b64` and
`signature_b64`. Version is integer 1. Base64 must be canonical RFC 4648 with
padding. Unknown key IDs fail against the caller's explicit trusted key map.
Ed25519 verifies the decoded bytes before parsing the payload. Payload SHA-256
is calculated over those same bytes. JSON is strict UTF-8; duplicate keys,
unknown fields, NUL strings, negative/nonintegral sizes, overflow, malformed
digests and unsupported versions fail closed.

Packages have complete file inventories. Snapshot unions must equal their
platform's entire base target by path, size and digest. Each language package
has exactly its declared `Data/texts-<locale>.pak`. Language files cannot appear
in base packages. All package references must exist and match kind/platform;
unreferenced packages are rejected. Every edge increases sequence, terminates
at or below the target sequence, and has unique package/file references.
Intermediate patch bytes may differ from the final target: the planner must
apply them in order and verify the final target before committing.

Paths obey ADR 0002. `.launcher`, launcher executables and generated status are
protected. `MODs` and `Profiles` require exact signed opt-ins. Linux executable
permissions come from the target record. Launch uses `bin/` and a managed
executable; Linux environment is restricted to the two documented variables.
No launch arguments are interpreted as shell syntax.

Sequences are positive uint64 integers. The verifier rejects a sequence lower
than the stored accepted value and a different payload at the same sequence.
Durable storage of this checkpoint is part of M4. A fresh installation has no
independent proof of freshness; authenticated HTTPS publication remains needed.
