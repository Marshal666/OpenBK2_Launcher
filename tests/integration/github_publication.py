"""Publication ordering and failure isolation; never calls GitHub."""
import base64
import hashlib
from pathlib import Path
import sys
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from fixture_builder import build_fixtures
from github_publish import ApiError, GitHubTransport, HttpsRedirect, PublicationIncident, publish_github
from release import sign_release
from signing import stable_json, strict_json
from test_workspace import temporary_workspace


class FakeGitHub:
    def __init__(self, directory, old=None, fail_at=None, existing=False, corrupt=False):
        self.directory, self.old, self.fail_at = directory, old, fail_at
        self.events, self.live, self.public, self.checked = [], None, False, set()
        self.existing, self.corrupt = existing, corrupt
        self.payload = strict_json((directory / "manifest.payload.json").read_bytes())

    def event(self, name):
        self.events.append(name)
        if len(self.events) == self.fail_at:
            raise ApiError(503)

    def api(self, method, url, data=None, file=None):
        if method == "GET" and "/contents/" in url:
            self.event("read prior manifest")
            if self.old is None:
                raise ApiError(404)
            return {"type": "file", "encoding": "base64", "content": base64.b64encode(self.old).decode(), "sha": "old-file-sha"}
        if method == "GET" and "/tags/" in url:
            self.event("find release")
            if self.existing:
                self.public = True
                return {"id": 1, "tag_name": "v3", "draft": False}
            raise ApiError(404)
        if method == "POST" and url.endswith("/releases"):
            self.event("create draft")
            assert data["draft"]
            return {"id": 1, "tag_name": "v3", "draft": True}
        if method == "GET" and "/assets?" in url:
            self.event("list assets")
            return [self.asset(p) for p in self.payload["packages"] if "/v3/" in p["url"]] if self.existing else []
        if method == "POST" and file:
            self.event("upload")
            assert not self.public
            return {"name": file.name, "size": file.stat().st_size, "digest": "sha256:"+hashlib.sha256(file.read_bytes()).hexdigest()}
        if method == "PATCH":
            self.event("publish assets")
            assert data["draft"] is False
            self.public = True
            return {}
        if method == "PUT":
            self.event("update fixed manifest")
            assert self.public and len(self.checked) == len(self.payload["packages"])
            if self.old:
                assert data["sha"] == "old-file-sha"
            self.live = base64.b64decode(data["content"])
            return {}
        raise AssertionError((method, url))

    def asset(self, package):
        return {"name": package["id"]+".zip", "size": package["size"],
                "digest": "sha256:"+("0"*64 if self.corrupt else package["sha256"])}

    def verify_public_asset(self, package):
        self.event("verify public asset")
        assert self.public
        assert hashlib.sha256((self.directory / (package["id"]+".zip")).read_bytes()).hexdigest() == package["sha256"]
        self.checked.add(package["id"])

    def public_bytes(self, url):
        self.event("verify fixed manifest")
        assert url == "https://raw.githubusercontent.com/test/manifests/main/manifest.envelope.json"
        return self.live


def main():
    with temporary_workspace() as temp:
        private = build_fixtures(temp / "fixtures")
        keys = {"fixture-only": private.public_key().public_bytes_raw()}
        for sequence in (1, 2, 3):
            directory = temp / f"fixtures/release-{sequence}"
            payload = strict_json((directory / "manifest.payload.json").read_bytes())
            for package in payload["packages"]:
                package["url"] = package["url"].replace("https://fixtures.invalid/releases/", "https://github.com/test/game/releases/download/v")
            (directory / "manifest.payload.json").write_bytes(stable_json(payload))
            sign_release(directory, private, "fixture-only")
        directory = temp / "fixtures/release-3"
        old = (temp / "fixtures/release-2/manifest.envelope.json").read_bytes()
        arguments = dict(owner="test", repo="game", tag="v3", manifest_repo="test/manifests", manifest_branch="main")
        fake = FakeGitHub(directory, old)
        plan = publish_github(directory, keys, transport=fake, dry_run=True, **arguments)
        assert plan["dry_run"] and fake.events == []
        publish_github(directory, keys, transport=fake, **arguments)
        assert fake.events[-2:] == ["update fixed manifest", "verify fixed manifest"]
        assert fake.live == (directory / "manifest.envelope.json").read_bytes()
        steps = len(fake.events)
        for at in range(1, steps+1):
            failing = FakeGitHub(directory, old, fail_at=at)
            try:
                publish_github(directory, keys, transport=failing, **arguments)
            except (ApiError, PublicationIncident) as error:
                if "update fixed manifest" in failing.events:
                    assert isinstance(error, PublicationIncident) and "BLOCKING" in str(error)
            else:
                raise AssertionError(f"Injected failure {at} was ignored")
            if failing.live is not None:
                assert failing.events[-1] == "verify fixed manifest"
        existing = FakeGitHub(directory, old, existing=True)
        publish_github(directory, keys, transport=existing, **arguments)
        assert "upload" not in existing.events and "publish assets" not in existing.events
        # A stale, but correctly signed CDN response can converge without
        # changing the manifest again or weakening sequence/signature checks.
        class StaleCDN(FakeGitHub):
            def public_bytes(self, url):
                live = super().public_bytes(url)
                return self.old if self.events.count("verify fixed manifest") == 1 else live
        stale = StaleCDN(directory, old)
        with patch("github_publish.time.sleep"):
            publish_github(directory, keys, transport=stale, **arguments)
        assert stale.events.count("verify fixed manifest") == 2
        assert stale.events.count("update fixed manifest") == 1
        class StuckCDN(FakeGitHub):
            def public_bytes(self, url):
                super().public_bytes(url)
                return self.old
        stuck = StuckCDN(directory, old)
        with patch("github_publish.time.sleep"):
            try:
                publish_github(directory, keys, transport=stuck, **arguments)
                raise AssertionError("Unverified fixed endpoint reported success")
            except PublicationIncident as error:
                assert "Do not publish an older sequence" in str(error)
        assert stuck.events.count("verify fixed manifest") == 4
        assert stuck.events.count("update fixed manifest") == 1
        corrupt = FakeGitHub(directory, old, existing=True, corrupt=True)
        try:
            publish_github(directory, keys, transport=corrupt, **arguments)
            raise AssertionError("Corrupt immutable asset accepted")
        except ValueError:
            assert "update fixed manifest" not in corrupt.events
        # Refuse old/different sequence before creating or modifying any asset.
        older_payload = strict_json((directory / "manifest.payload.json").read_bytes())
        older_payload["release"]["version"] = "redefined"
        from signing import sign_payload
        conflict = sign_payload(stable_json(older_payload), private, "fixture-only")
        conflict_transport = FakeGitHub(directory, conflict)
        try:
            publish_github(directory, keys, transport=conflict_transport, **arguments)
            raise AssertionError("Sequence redefinition accepted")
        except ValueError:
            assert conflict_transport.events == ["read prior manifest"]
        # Host guard must reject before allocating a connection or exposing a token.
        transport = GitHubTransport("test-placeholder-not-a-secret")
        for url in ("http://api.github.com/test", "https://example.com/api", "https://api.github.com@evil.invalid/"):
            try:
                transport.api("GET", url)
                raise AssertionError("Credential-host guard failed")
            except ValueError:
                pass
        import urllib.request
        for url in ("http://example.com/file", "file:///tmp/payload"):
            try:
                HttpsRedirect().redirect_request(urllib.request.Request("https://github.com/file"), None, 302, "", {}, url)
                raise AssertionError("Public download downgrade accepted")
            except ValueError:
                pass
    print(f"GitHub dry-run, all {steps} failure boundaries, immutable assets, replay guards, credential isolation and publication ordering passed")


if __name__ == "__main__":
    main()
