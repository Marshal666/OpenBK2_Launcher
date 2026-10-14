"""Developer GUI workflow. Reuses the release toolkit; never invokes a shell."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import quote

from github_publish import publish_github
from release import build_release, sign_release, verify_release
from signing import keygen, load_private, stable_json, strict_json


def require(condition, message):
    if not condition:
        raise ValueError(message)


def value(config, name, default=""):
    result = config.get(name, default)
    require(isinstance(result, str) and "\0" not in result, f"Invalid {name}")
    return result.strip()


def flag(config, name, default=False):
    result = config.get(name, default)
    require(isinstance(result, bool), f"Invalid {name}")
    return result


def lines(config, name):
    return [line.strip() for line in value(config, name).splitlines() if line.strip()]


def absolute_path(text, label):
    path = Path(text).expanduser()
    require(text and path.is_absolute(), f"Choose an absolute path for {label}.")
    return path.resolve()


def launcher_commands(source, config, manifest_url, public_key):
    """Return argument vectors so paths and punctuation never become shell code."""
    cmake = value(config, "cmake", "cmake") or "cmake"
    generator = value(config, "generator")
    build = source / "build/publisher-production"
    destination = source / "dist/production"
    configure = [cmake, "-S", str(source), "-B", str(build),
                 "-DOBK_FIXTURE_BUILD=OFF", "-DOBK_BUILD_GUI=ON",
                 "-DOBK_BUILD_PUBLISHER=OFF", "-DBUILD_TESTING=OFF",
                 "-DCMAKE_BUILD_TYPE=MinSizeRel", f"-DOBK_MANIFEST_URL={manifest_url}",
                 f"-DOBK_KEY_ID={value(config, 'key_id')}", f"-DOBK_PUBLIC_KEY_HEX={public_key}"]
    if generator:
        configure += ["-G", generator]
    if os.name == "nt" and (not generator or generator.startswith("Visual Studio")):
        configure += ["-A", "x64"]
    return [configure,
            [cmake, "--build", str(build), "--config", "MinSizeRel", "--parallel", "2"],
            [cmake, "--install", str(build), "--config", "MinSizeRel", "--prefix", str(destination)]]


def run_job(config, report=print):
    first_time = flag(config, "first_time", True)
    publish = flag(config, "publish", True)
    resume = flag(config, "resume")
    compile_launcher = flag(config, "build_launcher")
    source = Path(__file__).resolve().parents[1]
    owner, repo = value(config, "owner"), value(config, "repo")
    component = r"[A-Za-z0-9_.-]+"
    require(re.fullmatch(component, owner) and re.fullmatch(component, repo),
            "Enter the GitHub owner and existing repository name.")
    manifest_repo = value(config, "manifest_repo") or f"{owner}/{repo}"
    require(re.fullmatch(component + "/" + component, manifest_repo),
            "Manifest repository must be owner/repository, or blank to use the game repository.")
    branch = value(config, "manifest_branch", "main")
    require(branch and not any(c in branch for c in "\r\n?#\\"), "Enter the manifest branch.")
    key_id = value(config, "key_id")
    require(re.fullmatch(r"[A-Za-z0-9_-]+", key_id), "Key ID may contain letters, digits, _ and -.")
    version = value(config, "version")
    require(re.fullmatch(r"[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}(-[A-Za-z0-9.-]+)?", version),
            "Enter a game version such as 0.1.0 or 0.1.1.")
    sequence_text = value(config, "sequence")
    require(sequence_text.isascii() and sequence_text.isdigit(), "Sequence must be a positive integer.")
    sequence = int(sequence_text)
    require(0 < sequence <= 2**53 - 1, "Sequence is out of range.")
    tag = "v" + version
    base_url = f"https://github.com/{owner}/{repo}/releases/download/{quote(tag, safe='')}"
    manifest_url = f"https://raw.githubusercontent.com/{manifest_repo}/{quote(branch, safe='')}/manifest.envelope.json"
    output_root = absolute_path(value(config, "output_root"), "the releases folder")
    output = output_root / version
    private_file = absolute_path(value(config, "key_file"), "the private key file")
    require(not private_file.is_relative_to(source), "Keep the private key outside the launcher source checkout.")
    roots = {}
    for field, platform in (("windows_root", "windows-x64"), ("linux_root", "linux-x64")):
        if value(config, field):
            roots[platform] = absolute_path(value(config, field), field)
    optional_roots = {field: absolute_path(value(config, field), field) if value(config, field) else None
                      for field in ("shared_root", "shared_init_data", "mods_dir")}
    languages = absolute_path(value(config, "languages_dir"), "the language folder")
    previous = None
    if not first_time:
        previous = absolute_path(value(config, "previous_release"), "the previous release output")
        require(previous.is_dir(), "Previous release output folder does not exist.")
        require(previous != output, "The previous release and new output must be different folders.")
    require(roots, "Choose at least one platform folder (Windows or Linux).")
    for path in [*roots.values(), *optional_roots.values(), languages]:
        if path is None:
            continue
        require(resume or path.is_dir(), f"Input folder does not exist: {path}")
        require(not output.is_relative_to(path), "The release output must be outside all input folders.")
        require(not private_file.is_relative_to(path), "The private key must be outside every game input folder.")
    require(not private_file.is_relative_to(output_root), "Keep the private key outside the releases folder.")
    require(not publish or bool(os.environ.get("GITHUB_TOKEN")),
            "Enter a GitHub token, set GITHUB_TOKEN, or select Local check only.")
    require(not output.exists() or output.is_dir(), "Release output is not a directory.")
    require(resume or not output.exists() or not any(output.iterdir()),
            "This output already contains files. For an upload retry, enable Reuse signed output in More options. "
            "For changed game files, use a new version and sequence.")

    report("[1/6] Preparing signing key")
    if not private_file.exists():
        require(first_time and not resume, "Private key not found. Updates must reuse your original key.")
        require(not value(config, "public_key"), "Private key is missing but a public key is saved. Select the matching private key.")
        private_file.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        keygen(private_file)
        report(f"Created private key: {private_file}\nBack it up securely; reuse it for every release.")
    private = load_private(private_file)
    public_key = private.public_key().public_bytes_raw().hex()
    expected_key = value(config, "public_key").lower()
    require(not expected_key or expected_key == public_key, "The private key does not match the saved public key.")
    keys = {key_id: bytes.fromhex(public_key)}
    report(f"Public key: {public_key}")
    if previous:
        prior = verify_release(previous, keys)
        require(sequence > prior["release"]["sequence"], "The new sequence must exceed the previous release's sequence.")
        require(set(roots) >= set(prior["platforms"]), "Keep every previously supported platform in the update.")

    report("[2/6] Reusing signed output" if resume else "[2/6] Building game packages (large files can take a while)")
    if resume:
        manifest = verify_release(output, keys)
        require(manifest["release"]["version"] == version and manifest["release"]["sequence"] == sequence,
                "Existing signed output has a different version or sequence.")
        report("Using the frozen packages in the output folder; input file changes are not included.")
    else:
        build_release(out=output, version=version, sequence=sequence, roots=roots,
                      languages_dir=languages, shared_root=optional_roots["shared_root"],
                      shared_init_data=optional_roots["shared_init_data"],
                      mods_dir=optional_roots["mods_dir"], base_url=base_url, previous=previous,
                      allowlist=lines(config, "managed_files"), exclude=lines(config, "excludes"),
                      created_utc=datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"))
        report("[3/6] Signing release")
        sign_release(output, private, key_id)

    report("[4/6] Verifying signatures, packages and publication plan")
    arguments = dict(owner=owner, repo=repo, tag=tag, manifest_repo=manifest_repo, manifest_branch=branch)
    plan = publish_github(output, keys, dry_run=True, **arguments)
    report(f"Release: {plan['release']} | sequence {sequence} | {len(plan['uploads'])} uploads")
    report(f"Manifest: {manifest_url}")

    launcher_path = ""
    if compile_launcher:
        report("[5/6] Building the production launcher for this computer's OS")
        build_env = {k: v for k, v in os.environ.items() if k not in {"GITHUB_TOKEN", "OPENBK2_SIGNING_KEY_FILE"}}
        for command in launcher_commands(source, config, manifest_url, public_key):
            report("Running: " + subprocess.list2cmdline(command))
            subprocess.run(command, cwd=source, env=build_env, check=True, shell=False,
                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        name = "OpenBK2Launcher-Windows-x64.exe" if os.name == "nt" else "OpenBK2Launcher-Linux-x64"
        launcher_path = str(source / "dist/production" / name)
    else:
        report("[5/6] Launcher build skipped")

    if publish:
        report("[6/6] Publishing to GitHub and verifying public downloads\nThe fixed manifest advances after package verification.")
        publish_github(output, keys, **arguments)
        report("Publication complete. Players can check for updates.")
    else:
        report("[6/6] Local check complete. Nothing was uploaded.\nTo publish these exact files, clear Local check only and keep Reuse signed output enabled.")
    report(f"Retain the complete release folder: {output}")
    if launcher_path:
        report(f"Production launcher: {launcher_path}")
    return {"public_key": public_key, "release_dir": str(output), "version": version,
            "sequence": sequence, "published": publish, "manifest_url": manifest_url,
            "launcher_path": launcher_path}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", required=True, type=Path)
    parser.add_argument("--result", required=True, type=Path)
    args = parser.parse_args()
    result = {"success": False}
    try:
        config = strict_json(args.request.read_bytes())
        require(isinstance(config, dict), "Expected a publisher request object.")
        result.update(run_job(config, report=lambda message: print(message, flush=True)))
        result["success"] = True
    except Exception as error:
        message = str(error)
        token = os.environ.get("GITHUB_TOKEN", "")
        if token:
            message = message.replace(token, "[redacted]")
        result["error"] = message
        print("Stopped: " + message, flush=True)
    args.result.write_bytes(stable_json(result))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
