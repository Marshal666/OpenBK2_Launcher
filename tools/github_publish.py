"""Fail-closed GitHub publication. Asset URLs become live before the manifest changes."""
import base64
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request
from schema import require, safe_url, validate_payload
from signing import strict_json, verify_envelope

API = "https://api.github.com"
API_VERSION = "2026-03-10"


class ApiError(RuntimeError):
    def __init__(self, status):
        self.status = status
        super().__init__(f"GitHub API returned HTTP {status}; no existing asset was overwritten")


class PublicationIncident(RuntimeError):
    """The fixed manifest may have advanced; an older release is not a remedy."""


class PublicManifestPending(RuntimeError):
    """The public CDN still serves an authenticated earlier payload."""


class HttpsRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        safe_url(newurl)
        require(urllib.parse.urlsplit(newurl).scheme == "https", "Public redirect downgraded HTTPS")
        return super().redirect_request(request, fp, code, message, headers, newurl)


class GitHubTransport:
    def __init__(self, token):
        require(bool(token) and "\r" not in token and "\n" not in token, "Set GITHUB_TOKEN in environment or CI secrets")
        self._token = token
        self._public = urllib.request.build_opener(HttpsRedirect(), urllib.request.HTTPSHandler(context=ssl.create_default_context()))

    def api(self, method, url, data=None, file=None):
        parsed = urllib.parse.urlsplit(url)
        require(parsed.scheme == "https" and parsed.hostname in {"api.github.com", "uploads.github.com"}
                and parsed.port in {None, 443} and not parsed.username and not parsed.password and not parsed.fragment,
                "Credentials may only be sent to approved GitHub API hosts")
        require((file is None and parsed.hostname == "api.github.com") or
                (file is not None and method == "POST" and parsed.hostname == "uploads.github.com"), "Invalid API operation host")
        headers = {"Authorization": "Bearer " + self._token, "Accept": "application/vnd.github+json",
                   "User-Agent": "OpenBK2-Release/0.1", "X-GitHub-Api-Version": API_VERSION}
        body = json.dumps(data, separators=(",", ":")).encode() if data is not None else None
        stream = None
        if file is not None:
            stream = Path(file).open("rb")
            body = stream
            headers["Content-Type"] = "application/zip"
            headers["Content-Length"] = str(os.fstat(stream.fileno()).st_size)
        elif body is not None:
            headers["Content-Type"] = "application/json"
            headers["Content-Length"] = str(len(body))
        connection = http.client.HTTPSConnection(parsed.hostname, timeout=120, context=ssl.create_default_context())
        try:
            # http.client streams file objects in bounded blocks. API redirects are never followed.
            connection.request(method, urllib.parse.urlunsplit(("", "", parsed.path, parsed.query, "")), body, headers)
            response = connection.getresponse()
            if response.status >= 300:
                raise ApiError(response.status)
            result = response.read(4*1024*1024 + 1)
            require(len(result) <= 4*1024*1024, "Oversized API response")
            return strict_json(result) if result else None
        finally:
            if stream:
                stream.close()
            connection.close()

    def public_bytes(self, url, maximum=16*1024*1024):
        safe_url(url)
        request = urllib.request.Request(url, headers={"User-Agent": "OpenBK2-Release/0.1", "Cache-Control": "no-cache"})
        with self._public.open(request, timeout=60) as response:
            require(response.status == 200, "Public manifest unavailable")
            data = response.read(maximum + 1)
            require(len(data) <= maximum, "Oversized public manifest")
            return data

    def verify_public_asset(self, package):
        safe_url(package["url"])
        request = urllib.request.Request(package["url"], headers={"User-Agent": "OpenBK2-Release/0.1"})
        digest, count = hashlib.sha256(), 0
        with self._public.open(request, timeout=60) as response:
            require(response.status == 200, "Public package unavailable")
            while chunk := response.read(1024*1024):
                count += len(chunk)
                require(count <= package["size"], "Public package exceeds signed size")
                digest.update(chunk)
        require(count == package["size"] and digest.hexdigest() == package["sha256"], "Public package does not match signed bytes")


def retry_public(action):
    for attempt in range(4):
        try:
            return action()
        except (urllib.error.URLError, TimeoutError, OSError, PublicManifestPending):
            if attempt == 3:
                raise
            time.sleep(2**attempt)


def publish_github(directory, keys, *, owner, repo, tag, manifest_repo, manifest_branch,
                   manifest_path="manifest.envelope.json", dry_run=False, transport=None):
    # Import here to keep the release CLI's build/sign code independent of networking.
    from release import verify_release
    directory = Path(directory)
    manifest = verify_release(directory, keys)
    envelope = (directory / "manifest.envelope.json").read_bytes()
    component = r"[A-Za-z0-9_.-]+"
    require(re.fullmatch(component, owner) and re.fullmatch(component, repo), "Invalid GitHub repository name")
    require(re.fullmatch(component + "/" + component, manifest_repo), "Invalid manifest repository")
    require(tag and len(tag) < 150 and not any(c in tag for c in "\r\n?#\\"), "Invalid immutable release tag")
    require(manifest_branch and not any(c in manifest_branch for c in "\r\n?#\\"), "Invalid manifest branch")
    require(all(re.fullmatch(component, c) and c not in {".", ".."} for c in manifest_path.split("/")), "Invalid manifest file path")
    q = urllib.parse.quote
    base = f"https://github.com/{owner}/{repo}/releases/download/{q(tag, safe='')}"
    current = [p for p in manifest["packages"] if p["url"] == base + "/" + q(p["id"] + ".zip", safe="")]
    require(current, "No package URL points to this release tag; build with its immutable --base-url")
    # Historical patch assets can remain at their original immutable HTTPS URLs.
    require(all(p in current or p["kind"] == "patch" for p in manifest["packages"]), "Snapshot/language URL does not match intended release")
    fixed_url = f"https://raw.githubusercontent.com/{manifest_repo}/{q(manifest_branch, safe='')}/{q(manifest_path, safe='/')}"
    plan = {"release": f"{owner}/{repo}@{tag}", "uploads": [p["id"]+".zip" for p in current],
            "public_assets_to_verify": len(manifest["packages"]), "manifest_url": fixed_url,
            "sequence": manifest["release"]["sequence"], "dry_run": dry_run}
    if dry_run:
        return plan  # No API client, token requirement, or network activity.
    transport = transport or GitHubTransport(os.environ.get("GITHUB_TOKEN", ""))
    contents = f"{API}/repos/{manifest_repo}/contents/{q(manifest_path, safe='/')}"
    old_sha = None
    try:
        prior = transport.api("GET", contents + "?ref=" + q(manifest_branch, safe=""))
        require(prior.get("type") == "file" and prior.get("encoding") == "base64", "Existing manifest is not an ordinary base64 file")
        old_payload = verify_envelope(base64.b64decode("".join(prior["content"].split()), validate=True), keys)
        old = validate_payload(strict_json(old_payload))
        old_sequence = old["release"]["sequence"]
        require(manifest["release"]["sequence"] > old_sequence or
                (manifest["release"]["sequence"] == old_sequence and old_payload == (directory / "manifest.payload.json").read_bytes()),
                "Publishing would roll back or redefine an accepted sequence")
        old_sha = prior["sha"]
    except ApiError as error:
        if error.status != 404:
            raise
    releases = f"{API}/repos/{owner}/{repo}/releases"
    try:
        release = transport.api("GET", releases + "/tags/" + q(tag, safe=""))
    except ApiError as error:
        if error.status != 404:
            raise
        release = transport.api("POST", releases, {"tag_name": tag, "name": manifest["release"]["version"], "draft": True, "prerelease": False})
    require(release["tag_name"] == tag and isinstance(release["id"], int), "Release identity mismatch")
    release_url = releases + "/" + str(release["id"])
    assets = {}
    for page in range(1, 102):
        require(page < 101, "Too many existing assets")
        batch = transport.api("GET", release_url + f"/assets?per_page=100&page={page}")
        for asset in batch:
            require(asset["name"] not in assets, "Ambiguous duplicate release asset")
            assets[asset["name"]] = asset
        if len(batch) < 100:
            break
    # Construct an allowlisted upload endpoint ourselves; do not trust an API-supplied URL.
    upload = f"https://uploads.github.com/repos/{owner}/{repo}/releases/{release['id']}/assets?name="
    for package in current:
        name = package["id"] + ".zip"
        if name in assets:
            existing = assets[name]
            require(existing["size"] == package["size"] and existing.get("digest") == "sha256:"+package["sha256"],
                    "Existing immutable asset differs or has no authenticated API digest; refusing overwrite")
            continue
        require(release["draft"], "Published release is missing an asset; choose a new immutable tag")
        uploaded = transport.api("POST", upload+q(name, safe=""), file=directory / name)
        require(uploaded["name"] == name and uploaded["size"] == package["size"] and
                uploaded.get("digest") == "sha256:"+package["sha256"], "Uploaded asset digest mismatch")
    if release["draft"]:
        transport.api("PATCH", release_url, {"draft": False, "make_latest": "true"})
    for package in manifest["packages"]:
        retry_public(lambda p=package: transport.verify_public_asset(p))
    update = {"message": f"Publish verified OpenBK2 sequence {manifest['release']['sequence']}", "branch": manifest_branch,
              "content": base64.b64encode(envelope).decode("ascii")}
    if old_sha:
        update["sha"] = old_sha  # Optimistic concurrency: never clobber another publisher.
    def verify_fixed():
        live = transport.public_bytes(fixed_url)
        payload = verify_envelope(live, keys)
        if payload != (directory / "manifest.payload.json").read_bytes():
            public_manifest = validate_payload(strict_json(payload))
            if public_manifest["release"]["sequence"] < manifest["release"]["sequence"]:
                raise PublicManifestPending("Public endpoint still serves an earlier authenticated sequence")
            raise ValueError("Fixed endpoint serves a different accepted payload; refusing to overwrite it")
    try:
        # An interrupted PUT can have committed at GitHub. Its outcome is
        # uncertain; never promise that the public manifest stayed unchanged.
        transport.api("PUT", contents, update)
        retry_public(verify_fixed)
    except Exception as error:
        raise PublicationIncident("BLOCKING PUBLICATION INCIDENT: the fixed manifest may have advanced. "
            "Do not publish an older sequence. Inspect the public endpoint and retry this same verified "
            f"release to complete publication safely. Detail: {error}") from error
    return plan
