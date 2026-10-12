"""OpenBK2 complete-file ZIP release producer; Python 3.11+ only."""
import argparse
from datetime import datetime, timezone
import fnmatch
import hashlib
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import zipfile
from schema import PACKAGE_LIMIT, inventory, managed_path, paths_unique, require, safe_path, safe_url, validate_payload
from signing import keygen, load_private, sign_payload, stable_json, strict_json, verify_envelope

TOOL_VERSION = "0.1.0"


def file_hash(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def regular_file(path):
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and not (getattr(info, "st_file_attributes", 0) & 0x400), f"Not a plain unlinked regular file: {path}")


def file_record(path, relative, executable=False):
    regular_file(path)
    safe_path(relative)
    return {"path": relative, "size": path.stat().st_size, "sha256": file_hash(path), "executable": executable}


def scan_root(root, platform, allowlist=(), exclude=()):
    root = Path(root).resolve(strict=True)
    records, sources = [], {}
    for directory, dirs, names in os.walk(root, followlinks=False):
        for name in dirs[:]:
            path = Path(directory, name)
            rel = path.relative_to(root).as_posix()
            require(not path.is_symlink() and not (getattr(path.lstat(), "st_file_attributes", 0) & 0x400), f"Linked directory rejected: {rel}")
            if rel.lower() == ".launcher":
                dirs.remove(name)
        for name in sorted(names):
            path = Path(directory, name)
            rel = path.relative_to(root).as_posix()
            lower = rel.lower()
            if lower == "launcher_installed.txt" or name.lower().startswith("openbk2launcher") or lower.startswith("data/texts-"):
                continue
            if lower.startswith(("mods/", "profiles/")) and rel not in allowlist:
                continue
            if any(fnmatch.fnmatchcase(rel, pattern) for pattern in exclude):
                continue
            managed_path(rel, set(allowlist))
            executable = rel == ("bin/Game.exe" if platform == "windows-x64" else "bin/Game") or (platform == "linux-x64" and bool(path.stat().st_mode & 0o111))
            records.append(file_record(path, rel, executable))
            sources[rel] = path
    records.sort(key=lambda f: f["path"])
    paths_unique(f["path"] for f in records)
    require(all(path in sources for path in allowlist), "Allowlisted official file is missing from source")
    return records, sources


def make_packages(out, base_url, kind, platform, records, sources, limit=PACKAGE_LIMIT):
    # Reserve header and worst-case deflate overhead using uncompressed sizes.
    # One file cannot be split in schema v1.
    batches, current, size = [], [], 22
    for f in sorted(records, key=lambda f: f["path"]):
        reserve = f["size"] + f["size"] // 1000 + 4096 + 2 * len(f["path"].encode())
        require(reserve + 22 < limit, f"File cannot fit below package limit: {f['path']}")
        if (size + reserve >= limit or len(current) >= 60000) and current:
            batches.append(current)
            current, size = [], 22
        current.append(f)
        size += reserve
    if current:
        batches.append(current)
    result = []
    for batch in batches:
        temp = out / "package.building"
        require(not temp.exists(), "Stale package.building file; inspect it before retrying")
        try:
            with zipfile.ZipFile(temp, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9, allowZip64=True) as archive:
                for f in batch:
                    entry = zipfile.ZipInfo(f["path"], date_time=(1980, 1, 1, 0, 0, 0))
                    entry.create_system = 3
                    entry.external_attr = (stat.S_IFREG | (0o755 if f.get("executable") else 0o644)) << 16
                    entry.compress_type = zipfile.ZIP_DEFLATED
                    entry._compresslevel = 9
                    regular_file(sources[f["path"]])
                    with Path(sources[f["path"]]).open("rb") as source, archive.open(entry, "w", force_zip64=True) as target:
                        shutil.copyfileobj(source, target, 1024*1024)
            digest = file_hash(temp)
            name = f"{kind}-{platform}-{digest}.zip"
            destination = out / name
            if destination.exists():
                require(file_hash(destination) == digest, "Content-addressed filename collision")
                temp.unlink()
            else:
                temp.rename(destination)
            require(destination.stat().st_size < limit, "Compressed package exceeds limit")
            result.append({"id": name[:-4], "kind": kind, "platform": platform, "url": base_url.rstrip("/")+"/"+name,
                           "size": destination.stat().st_size, "unpacked_size": sum(f["size"] for f in batch),
                           "sha256": digest, "files": batch})
        finally:
            if temp.exists():
                temp.unlink()
    return result


def build_release(*, out, version, sequence, roots, languages_dir, base_url, previous=None, allowlist=(), exclude=(),
                  created_utc="1970-01-01T00:00:00Z", package_limit=PACKAGE_LIMIT):
    out = Path(out).resolve()
    require(not out.exists() or not any(out.iterdir()), "Output directory must be empty")
    safe_url(base_url.rstrip("/")+"/")
    for root in roots.values():
        require(not out.is_relative_to(Path(root).resolve()), "Output cannot be inside a game source root")
    out.mkdir(parents=True, exist_ok=True)
    old = None
    if previous:
        previous = Path(previous).resolve(strict=True)
        old = verify_release(previous, allow_unsigned=True)
        require(sequence > old["release"]["sequence"], "New sequence must increase")
    m = {"schema_version": 1, "release": {"version": version, "sequence": sequence, "created_utc": created_utc},
         "min_launcher_version": "0.1.0", "packages": [], "platforms": {}, "languages": {}}
    histories = {}
    if old:
        metadata = strict_json((previous / "release-metadata.json").read_bytes())
        histories.update(metadata.get("history", {}))
        histories[str(old["release"]["sequence"])] = old["platforms"]
    for platform, root in roots.items():
        records, sources = scan_root(root, platform, allowlist, exclude)
        snapshots = make_packages(out, base_url, "snapshot", platform, records, sources, package_limit)
        m["packages"].extend(snapshots)
        launch = {"executable": "bin/Game.exe" if platform == "windows-x64" else "bin/Game", "working_directory": "bin", "args": [],
                  "environment": {} if platform == "windows-x64" else {"DXVK_WSI_DRIVER": "SDL3", "LD_LIBRARY_PATH": "${INSTALL_DIR}/bin"}}
        target = {"launch": launch, "files": records, "snapshot_package_ids": [p["id"] for p in snapshots],
                  "managed_user_dir_files": sorted(allowlist), "update_edges": []}
        if old and platform in old["platforms"]:
            prior = old["platforms"][platform]
            previous_files = {f["path"]: f for f in prior["files"]}
            changed = [f for f in records if previous_files.get(f["path"]) != f]
            patches = make_packages(out, base_url, "patch", platform, changed, sources, package_limit)
            m["packages"].extend(patches)
            target["update_edges"] = prior["update_edges"] + [{"from_sequence": old["release"]["sequence"], "to_sequence": sequence, "package_ids": [p["id"] for p in patches]}]
            # Retain exact old opt-ins while advertised historical patch entries need them.
            target["managed_user_dir_files"] = sorted(set(allowlist) | set(prior["managed_user_dir_files"]))
            needed = {pid for edge in prior["update_edges"] for pid in edge["package_ids"]}
            for p in old["packages"]:
                if p["id"] in needed:
                    m["packages"].append(p)
                    shutil.copyfile(previous / (p["id"]+".zip"), out / (p["id"]+".zip"))
        m["platforms"][platform] = target
    for source in sorted(Path(languages_dir).iterdir()):
        match = re.fullmatch(r"texts-([a-z]{2,3}(?:-[A-Za-z0-9]{2,8}){0,2})\.pak", source.name)
        require(match is not None, f"Unexpected language input: {source.name}")
        locale = match[1]
        f = file_record(source, "Data/"+source.name)
        p = make_packages(out, base_url, "language", "any", [f], {f["path"]: source}, package_limit)[0]
        m["packages"].append(p)
        m["languages"][locale] = {"label": {"en": "English", "de": "Deutsch"}.get(locale, locale),
                                   "path": f["path"], "size": f["size"], "sha256": f["sha256"], "package_id": p["id"]}
    validate_payload(m)
    (out / "manifest.payload.json").write_bytes(stable_json(m))
    (out / "release-metadata.json").write_bytes(stable_json({"tool_version": TOOL_VERSION, "history": histories}))
    verify_release(out, allow_unsigned=True)
    return m


def verify_release(directory, trusted_keys=None, allow_unsigned=False):
    directory = Path(directory)
    payload = (directory / "manifest.payload.json").read_bytes()
    if trusted_keys is not None:
        authenticated = verify_envelope((directory / "manifest.envelope.json").read_bytes(), trusted_keys)
        require(authenticated == payload, "Envelope and payload differ")
    else:
        require(allow_unsigned, "Verification requires an independently trusted public key")
    m = validate_payload(strict_json(payload))
    for p in m["packages"]:
        path = directory / (p["id"]+".zip")
        regular_file(path)
        require(path.stat().st_size == p["size"] and file_hash(path) == p["sha256"], f"Corrupt package: {p['id']}")
        expected = {f["path"]: f for f in p["files"]}
        with zipfile.ZipFile(path) as archive:
            entries = archive.infolist()
            require(len(entries) <= 60000, "ZIP entry limit exceeded")
            paths_unique(e.filename for e in entries)
            require({e.filename for e in entries} == expected.keys(), "Unexpected ZIP entries")
            for entry in entries:
                require(not entry.is_dir() and entry.compress_type in {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED} and not (entry.flag_bits & 1), "Unsupported ZIP entry")
                mode = entry.external_attr >> 16
                require(stat.S_IFMT(mode) in {0, stat.S_IFREG}, "Linked or special ZIP entry")
                f = expected[entry.filename]
                require(entry.file_size <= max(entry.compress_size, 1) * 2000, "ZIP expansion ratio exceeds launcher limit")
                require(entry.file_size == f["size"], "ZIP file size mismatch")
                hasher, size = hashlib.sha256(), 0
                with archive.open(entry) as source:
                    while data := source.read(65536):
                        size += len(data)
                        require(size <= f["size"], "Expanded file exceeds signed limit")
                        hasher.update(data)
                require(size == f["size"] and hasher.hexdigest() == f["sha256"], "Extracted digest mismatch")
    # Replay each advertised historical route using inventories whose bytes were
    # verified above. Intermediate replacements must produce the newest target.
    history = strict_json((directory / "release-metadata.json").read_bytes()).get("history", {})
    packages = {p["id"]: p for p in m["packages"]}
    for platform, target in m["platforms"].items():
        for sequence, platforms in history.items():
            if platform not in platforms:
                continue
            current = {f["path"]: f for f in platforms[platform]["files"]}
            position = int(sequence)
            while position < m["release"]["sequence"]:
                edges = [edge for edge in target["update_edges"] if edge["from_sequence"] == position]
                if not edges:
                    break  # Not advertised: latest snapshot is the supported route.
                require(len(edges) == 1, "Ambiguous historical edge lineage")
                edge = edges[0]
                for pid in edge["package_ids"]:
                    current.update({f["path"]: f for f in packages[pid]["files"]})
                position = edge["to_sequence"]
            if position == m["release"]["sequence"]:
                for f in target["files"]:
                    require(current.get(f["path"]) == f, "Patch chain cannot construct target")
    return m


def sign_release(directory, private_key, key_id):
    directory = Path(directory)
    verify_release(directory, allow_unsigned=True)
    envelope = sign_payload((directory / "manifest.payload.json").read_bytes(), private_key, key_id)
    (directory / "manifest.envelope.json").write_bytes(envelope)
    verify_release(directory, {key_id: private_key.public_key().public_bytes_raw()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    k = sub.add_parser("keygen")
    k.add_argument("--output-private", required=True, type=Path)
    b = sub.add_parser("build")
    for name in ("version", "base-url", "languages-dir", "out"):
        b.add_argument("--"+name, required=True)
    b.add_argument("--sequence", required=True, type=int)
    b.add_argument("--windows-root")
    b.add_argument("--linux-root")
    b.add_argument("--previous-release")
    b.add_argument("--managed-user-file", action="append", default=[])
    b.add_argument("--exclude", action="append", default=[])
    b.add_argument("--created-utc", default=None)
    v = sub.add_parser("verify")
    v.add_argument("--release-dir", required=True, type=Path)
    v.add_argument("--key-id", required=True)
    v.add_argument("--public-key", required=True, help="Independently trusted public key as 64 hex characters")
    s = sub.add_parser("sign")
    s.add_argument("--release-dir", required=True, type=Path)
    s.add_argument("--private-key", type=Path, default=os.environ.get("OPENBK2_SIGNING_KEY_FILE"))
    s.add_argument("--key-id", required=True)
    p = sub.add_parser("publish-github")
    for name in ("release-dir", "owner", "repo", "tag", "manifest-repo", "manifest-branch", "key-id", "public-key"):
        p.add_argument("--" + name, required=True)
    p.add_argument("--manifest-path", default="manifest.envelope.json")
    p.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        if args.command == "keygen":
            print("Public key:", keygen(args.output_private))
        elif args.command == "build":
            roots = {p: r for p, r in [("windows-x64", args.windows_root), ("linux-x64", args.linux_root)] if r}
            timestamp = args.created_utc or datetime.fromtimestamp(int(os.environ.get("SOURCE_DATE_EPOCH", "0")), timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
            build_release(out=args.out, version=args.version, sequence=args.sequence, roots=roots,
                          languages_dir=args.languages_dir, base_url=args.base_url, previous=args.previous_release,
                          allowlist=args.managed_user_file, exclude=args.exclude, created_utc=timestamp)
            print("Built and verified unsigned release:", args.out)
        elif args.command == "verify":
            verify_release(args.release_dir, {args.key_id: bytes.fromhex(args.public_key)})
            print("Signature, packages, target inventories, and advertised chains verified")
        elif args.command == "sign":
            require(args.private_key is not None, "Supply --private-key or OPENBK2_SIGNING_KEY_FILE")
            require(not Path(args.private_key).resolve().is_relative_to(Path(__file__).resolve().parents[1]), "Private key must be outside repository")
            sign_release(args.release_dir, load_private(args.private_key), args.key_id)
            print("Signed and verified:", args.release_dir)
        elif args.command == "publish-github":
            from github_publish import publish_github
            result = publish_github(args.release_dir, {args.key_id: bytes.fromhex(args.public_key)}, owner=args.owner,
                repo=args.repo, tag=args.tag, manifest_repo=args.manifest_repo, manifest_branch=args.manifest_branch,
                manifest_path=args.manifest_path, dry_run=args.dry_run)
            print(stable_json(result).decode("utf-8"))
    except Exception as error:
        parser.exit(1, f"Release failed: {error}\n")


if __name__ == "__main__":
    main()
