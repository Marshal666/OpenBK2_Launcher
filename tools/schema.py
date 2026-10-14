"""Publisher-side validation of signed payload v1/v2/v3 and portable file paths."""
import re
from urllib.parse import urlsplit

PACKAGE_LIMIT = 1536 * 1024 * 1024
UNPACK_LIMIT = 64 * 1024**3
PLATFORMS = {"windows-x64", "linux-x64"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def fields(value, required, optional=()):
    require(isinstance(value, dict) and set(required) <= value.keys() <= set(required) | set(optional), "Unknown/missing schema fields")


def integer(value, maximum=2**64-1):
    require(type(value) is int and 0 <= value <= maximum, "Invalid unsigned integer")
    return value


def digest(value):
    require(isinstance(value, str) and re.fullmatch(r"[a-f0-9]{64}", value), "Invalid SHA-256")


def safe_path(path):
    require(isinstance(path, str) and 0 < len(path) <= 1024 and path.isascii(), "Nonportable game path")
    parts = path.split("/")
    require(len(parts) <= 32, "Path depth exceeded")
    for part in parts:
        require(part and len(part) <= 255 and part not in {".", ".."} and not part.endswith((".", " ")), "Unsafe path component")
        require(not any(ord(c) < 32 or ord(c) > 126 or c in '\\:<>"|?*' for c in part), "Unsafe path character")
        stem = part.split(".", 1)[0].lower()
        require(stem not in {"con", "prn", "aux", "nul", "clock$"} and not re.fullmatch(r"(com|lpt)[0-9]", stem), "Windows reserved name")
    return path


def managed_path(path, allowlist):
    safe_path(path)
    lower = path.lower()
    require(not (lower == ".launcher" or lower.startswith(".launcher/") or lower == "launcher_installed.txt" or lower.split("/")[-1].startswith("openbk2launcher")), "Protected launcher path")
    require(not lower.startswith(("mods/", "profiles/")) or path in allowlist, "Missing exact user-directory allowlist")


def paths_unique(paths):
    nodes, files = {}, set()
    for path in paths:
        safe_path(path)
        parts = path.split("/")
        for i in range(1, len(parts)+1):
            prefix = "/".join(parts[:i])
            key = prefix.lower()
            require(key not in files and nodes.get(key, prefix) == prefix, "Path alias or file/directory conflict")
            if i == len(parts):
                require(key not in nodes, "Duplicate path or directory/file conflict")
                files.add(key)
            nodes[key] = prefix


def safe_url(url, fixture=False):
    require(isinstance(url, str) and len(url) <= 4096 and url.isascii() and not any(ord(c) <= 32 or c in "\\#" for c in url), "Unsafe URL")
    parsed = urlsplit(url)
    require(parsed.scheme == "https" or (fixture and parsed.scheme == "http" and parsed.hostname == "127.0.0.1"), "HTTPS required")
    require(parsed.hostname and parsed.path.startswith("/") and not parsed.username and not parsed.password and "%" not in parsed.netloc, "Unsafe URL authority")


def inventory(items, target=False):
    require(isinstance(items, list) and len(items) <= 100000, "Invalid inventory")
    for f in items:
        fields(f, {"path", "size", "sha256", "executable"} if target else {"path", "size", "sha256"}, () if target else {"executable"})
        safe_path(f["path"])
        integer(f["size"], PACKAGE_LIMIT)
        digest(f["sha256"])
        if "executable" in f:
            require(type(f["executable"]) is bool, "Invalid executable flag")
    paths_unique(f["path"] for f in items)
    return {f["path"]: f for f in items}


def same_bytes(a, b):
    return all(a[k] == b[k] for k in ("path", "size", "sha256"))


def validate_payload(m, fixture=False):
    schema = m.get("schema_version")
    require(type(schema) is int and schema in {1, 2, 3}, "Unsupported schema")
    fields(m, {"schema_version", "release", "min_launcher_version", "packages", "platforms", "languages"} | ({"mods"} if schema >= 2 else set()) | ({"initial_data"} if schema >= 3 else set()))
    fields(m["release"], {"version", "sequence", "created_utc"})
    require(re.fullmatch(r"[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}(-[A-Za-z0-9.-]+)?", m["release"]["version"]), "Invalid version")
    sequence = integer(m["release"]["sequence"])
    require(sequence > 0, "Sequence must be positive")
    require(re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", m["release"]["created_utc"]), "Invalid UTC timestamp")
    minimum = m["min_launcher_version"]
    require(isinstance(minimum, str) and re.fullmatch(r"[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}", minimum), "Invalid minimum launcher version")
    minimum = tuple(map(int, minimum.split(".")))
    require(minimum <= (0, 3, 0), "Unsupported minimum launcher version")
    require(schema == 1 or minimum >= (0, 2, 0), "Schema v2 requires launcher 0.2.0")
    require(schema < 3 or minimum >= (0, 3, 0), "Schema v3 requires launcher 0.3.0")
    packages, used = {}, set()
    require(isinstance(m["packages"], list), "Invalid packages")
    for p in m["packages"]:
        fields(p, {"id", "kind", "platform", "url", "size", "unpacked_size", "sha256", "files"})
        require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", p["id"]) and p["id"] not in packages, "Invalid/duplicate package ID")
        require(p["kind"] in ({"snapshot", "patch", "language"} | ({"mod"} if schema >= 2 else set()) | ({"initial"} if schema >= 3 else set())), "Invalid package kind")
        require(p["platform"] == "any" if p["kind"] in {"language", "mod", "initial"} else p["platform"] in (PLATFORMS | ({"any"} if schema >= 2 else set())), "Invalid package platform")
        safe_url(p["url"], fixture)
        require(integer(p["size"], PACKAGE_LIMIT) > 0, "Empty ZIP")
        integer(p["unpacked_size"], UNPACK_LIMIT)
        digest(p["sha256"])
        files = inventory(p["files"])
        require(files and sum(f["size"] for f in files.values()) == p["unpacked_size"], "Unpacked size mismatch")
        packages[p["id"]] = p

    def package(pid, kind, platform):
        p = packages[pid]
        require(p["kind"] == kind and (p["platform"] == platform or (schema >= 2 and kind in {"snapshot", "patch"} and p["platform"] == "any")), "Package membership mismatch")
        used.add(pid)
        return p

    require(isinstance(m["platforms"], dict) and 0 < len(m["platforms"]) <= 2, "Invalid platform list")
    for platform, target in m["platforms"].items():
        require(platform in PLATFORMS, "Unknown platform")
        fields(target, {"launch", "files", "snapshot_package_ids", "managed_user_dir_files", "update_edges"})
        files = inventory(target["files"], True)
        require(files, "Empty target")
        require(sum(f["size"] for f in files.values()) <= 512*1024**3, "Target size limit exceeded")
        allowed = target["managed_user_dir_files"]
        require(isinstance(allowed, list) and len(allowed) == len(set(allowed)), "Duplicate allowlist")
        for path in allowed:
            safe_path(path)
            require(path.lower().startswith(("mods/", "profiles/")), "Invalid allowlist location")
        for path in files:
            managed_path(path, allowed)
            require(not path.lower().startswith("data/texts-"), "Language in base files")
        snapshots = []
        require(len(target["snapshot_package_ids"]) == len(set(target["snapshot_package_ids"])), "Duplicate snapshot reference")
        for pid in target["snapshot_package_ids"]:
            snapshots.extend(package(pid, "snapshot", platform)["files"])
        snap = inventory(snapshots)
        require(snap.keys() == files.keys() and all(same_bytes(snap[p], files[p]) for p in files), "Snapshot target mismatch")
        edges = set()
        for edge in target["update_edges"]:
            fields(edge, {"from_sequence", "to_sequence", "package_ids"})
            pair = integer(edge["from_sequence"]), integer(edge["to_sequence"])
            require(0 < pair[0] < pair[1] <= sequence and pair not in edges, "Invalid update edge")
            edges.add(pair)
            require(len(edge["package_ids"]) == len(set(edge["package_ids"])), "Duplicate patch reference")
            edge_files = []
            for pid in edge["package_ids"]:
                edge_files.extend(package(pid, "patch", platform)["files"])
            inventory(edge_files)
            for f in edge_files:
                managed_path(f["path"], allowed)
                require(not f["path"].lower().startswith("data/texts-"), "Language in patch")
        launch = target["launch"]
        fields(launch, {"executable", "working_directory", "args", "environment"})
        require(launch["working_directory"] == "bin" and launch["executable"].startswith("bin/") and files.get(launch["executable"], {}).get("executable"), "Unsafe launch profile")
        require(isinstance(launch["args"], list) and len(launch["args"]) <= 32 and all(isinstance(a, str) and len(a.encode()) <= 1024 and "\0" not in a for a in launch["args"]), "Unsafe launch arguments")
        env = {"DXVK_WSI_DRIVER": "SDL3", "LD_LIBRARY_PATH": "${INSTALL_DIR}/bin"} if platform == "linux-x64" else {}
        require(launch["environment"] == env, "Unsafe environment")
    require(isinstance(m["languages"], dict) and 0 < len(m["languages"]) <= 64, "Invalid languages")
    for locale, lang in m["languages"].items():
        fields(lang, {"label", "path", "package_id", "size", "sha256"})
        require(re.fullmatch(r"[a-z]{2,3}(-[A-Za-z0-9]{2,8}){0,2}", locale), "Invalid locale")
        require(isinstance(lang["label"], str) and 0 < len(lang["label"].encode()) <= 128, "Invalid language label")
        require(lang["path"] == f"Data/texts-{locale}.pak", "Invalid language path")
        integer(lang["size"], PACKAGE_LIMIT)
        digest(lang["sha256"])
        p = package(lang["package_id"], "language", "any")
        require(len(p["files"]) == 1 and same_bytes(p["files"][0], lang), "Language inventory mismatch")
        for target in m["platforms"].values():
            paths_unique([f["path"] for f in target["files"]] + [lang["path"]])
    paths_unique(lang["path"] for lang in m["languages"].values())
    if schema >= 2:
        require(isinstance(m["mods"], dict) and len(m["mods"]) <= 256, "Invalid mod catalog")
        paths_unique(m["mods"].keys())
        for mid, mod in m["mods"].items():
            require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", mid), "Invalid mod identifier")
            fields(mod, {"label", "files", "package_ids"})
            require(isinstance(mod["label"], str) and 0 < len(mod["label"].encode()) <= 128, "Invalid mod label")
            files = inventory(mod["files"], True)
            require(files and sum(f["size"] for f in files.values()) <= UNPACK_LIMIT, "Invalid mod size")
            require(len(mod["package_ids"]) == len(set(mod["package_ids"])), "Duplicate mod package reference")
            packaged = inventory([f for pid in mod["package_ids"] for f in package(pid, "mod", "any")["files"]])
            require(files.keys() == packaged.keys() and all(same_bytes(f, packaged[path]) for path, f in files.items()), "Mod package coverage mismatch")
            prefix = "MODs/"+mid+"/"
            for path in files:
                require(path.startswith(prefix), "Mod file outside its folder")
                managed_path(path, {path})
            for target in m["platforms"].values():
                folder = prefix[:-1].lower()
                for f in target["files"]:
                    path = f["path"].lower()
                    require(path != folder and not path.startswith(folder+"/") and not folder.startswith(path+"/"), "Optional mod overlaps base game")
    if schema >= 3:
        initial = m["initial_data"]
        fields(initial, {"folders", "files", "package_ids"})
        require(isinstance(initial["folders"], list) and len(initial["folders"]) <= 64, "Invalid initial folders")
        paths_unique(initial["folders"])
        for folder in initial["folders"]:
            require("/" not in folder and folder.lower() not in {".launcher", "bin", "data", "mods", "launcher_installed.txt"} and not folder.lower().startswith("openbk2launcher"), "Unsafe initial-data folder")
        files = inventory(initial["files"], True)
        require(sum(f["size"] for f in files.values()) <= UNPACK_LIMIT, "Initial-data size limit exceeded")
        require({path.split('/')[0] for path in files} == set(initial["folders"]), "Initial-data folders must contain files")
        for path, f in files.items():
            require('/' in path and not f['executable'], "Initial data must be non-executable files inside folders")
            managed_path(path, {path})
        require(len(initial["package_ids"]) == len(set(initial["package_ids"])), "Duplicate initial package reference")
        packaged = inventory([f for pid in initial["package_ids"] for f in package(pid, "initial", "any")["files"]])
        require(files.keys() == packaged.keys() and all(same_bytes(f, packaged[path]) for path, f in files.items()), "Initial package coverage mismatch")
        folders = {folder.lower() for folder in initial["folders"]}
        for target in m["platforms"].values():
            require(not any(f["path"].split('/')[0].lower() in folders for f in target["files"]), "Initial-data folders overlap managed game files")
    require(used == packages.keys(), "Unreferenced package")
    return m
