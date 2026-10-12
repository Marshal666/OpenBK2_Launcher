# Publisher toolkit

Python 3.11+ and `python -m pip install -r tools/requirements.txt` are developer
requirements only. No Python is bundled into or required by the launcher.

```text
python tools/release.py keygen --output-private /secure/outside-repo/release.pem
python tools/release.py build --version 0.1.0 --sequence 1 --windows-root <root> --linux-root <root> --languages-dir <languages> --base-url https://github.com/OWNER/REPO/releases/download/v0.1.0 --created-utc 2026-10-12T08:00:00Z --out <empty-output>
python tools/release.py sign --release-dir <output> --private-key <external-private-file> --key-id release-key-2026-01
python tools/release.py verify --release-dir <output> --key-id release-key-2026-01 --public-key <independently-trusted-64-hex-public-key>
```

Keep the private key outside Git, in a secure directory with restrictive ACLs
on Windows / permissions on Linux. Back it up securely. The keygen command
refuses existing files and repository destinations and prints only the public
key. Signing also accepts `OPENBK2_SIGNING_KEY_FILE`; never pass key bytes on a
command line. Production verification keys must be supplied independently,
not taken from untrusted downloaded release metadata.

Language input contains only `texts-en.pak`, `texts-de.pak`, etc. Roots contain
game-root-relative `bin/`, `Data/` etc. Use `--managed-user-file MODs/official.pak`
for each exact official file and `--exclude <glob>` for mutable/generated files.
Unknown MODs/Profiles files are excluded. Symlinks, reparse points, hardlinked
regular files, reserved paths and output directories nested in source roots
are rejected. Private source files are not auto-discovered outside supplied roots.

For later releases pass `--previous-release <prior-output>`. The publisher
validates all previous package bytes and computes SHA-256 differences, retains
available earlier edges and assets, and tests each advertised route against the
new final inventory. Previous-release metadata is a local publisher input,
not a remote trust anchor. Do not take it from an untrusted third party.

ZIP entries are sorted, use fixed 1980 timestamps and modes, and use deflate
level 9. Filenames include the complete archive digest. `--created-utc` or
`SOURCE_DATE_EPOCH` fixes payload timestamps (the fallback is the Unix epoch).
Same toolchain/input yields identical ZIP and payload bytes; different zlib
versions can change deflate output. Determinism is tested separately from
Ed25519 authenticity. Packages are split below 1.5 GiB, reserving ZIP overhead.
A single too-large file is rejected; no multipart-file mechanism is implied.

Publishing uses GitHub's REST API from Python. Set `GITHUB_TOKEN` through your
secret manager or trusted CI environment. It needs Contents write access to the
asset repository and the manifest repository. Public downloads never receive it.

```text
python tools/release.py publish-github --release-dir <output> --owner OWNER --repo GAME_ASSETS --tag v0.1.0 --manifest-repo OWNER/MANIFESTS --manifest-branch main --key-id release-key-2026-01 --public-key <trusted-64-hex-key> --dry-run
```

Dry-run fully verifies the local release and prints a plan without creating a
network client or requiring a token. Remove `--dry-run` to publish. The default
manifest path is `manifest.envelope.json`; `--manifest-path` can override it.
The printed raw GitHub URL is the fixed endpoint to compile into the launcher.
Build each new release with its immutable GitHub `--base-url` shown above.
Historical patch URLs retain their original release tag.

The command authenticates the existing fixed envelope and checks sequence
monotonicity, creates/resumes a draft release, uploads missing immutable assets,
checks GitHub's upload digest, publishes the asset release, then independently
streams every public package and verifies length/SHA-256. Only after all public
assets pass does it update the fixed envelope using the current file SHA as a
concurrency guard. It re-fetches and verifies the fixed public envelope last.
Existing assets are never overwritten or deleted. An existing asset without an
API digest is refused conservatively. Partial drafts can be resumed; a missing
asset on an already published release requires a new immutable tag. A failure
after attempting the fixed-envelope update is a **blocking publication incident**:
even a lost PUT response can mean the update committed. The publisher retries
transient public fetch failures and correctly signed stale CDN responses with
bounded backoff; a different same/newer payload is never overwritten. If public
verification still fails, inspect visibility and rerun the same verified release
as a forward repair. Never publish an older sequence to hide the incident.

Release checklist:

1. Build/test both native launchers, inspect OS dependencies and enforce 15 MiB.
2. Assemble authorized built game roots, explicit mutable-file exclusions and
   exact official MODs/Profiles opt-ins. Keep user files out of source inputs.
3. Build against the prior verified local release and a new tag/sequence. Use a
   fixed release timestamp. Verify snapshots, language packs and advertised chains.
4. Sign with the offline/external key; verify with an independently trusted public
   key. Review the payload, inventories, sizes and final launch profiles.
5. Run publication dry-run, then publish using protected CI or a release account.
6. Verify the public fixed endpoint and packages from another machine; run a
   small authorized real-game installation before announcing the release.
7. Retain earlier immutable patch assets while a latest manifest references them.

API contracts: [releases](https://docs.github.com/en/rest/releases/releases),
[assets](https://docs.github.com/en/rest/releases/assets), and
[repository contents](https://docs.github.com/en/rest/repos/contents).
Tests use a transport double with failures at every publication boundary; no
live GitHub publication was performed during implementation.
