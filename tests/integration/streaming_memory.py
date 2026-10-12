"""Measure native download/hash memory using a 64 MiB synthetic signed ZIP.

This measures launcher_core in transfer_check, not GUI or publisher memory.
The native driver verifies the signed envelope, downloaded length and SHA-256.
"""
import ctypes
from ctypes import wintypes as w
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import threading
import time
import zipfile
from test_workspace import temporary_workspace
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from fixture_builder import build_fixtures
from signing import sign_payload, stable_json, strict_json


def main():
    driver, output = Path(sys.argv[1]).resolve(strict=True), Path(sys.argv[2])
    with temporary_workspace() as temp:
        key = build_fixtures(temp / "fixtures")
        release = temp / "fixtures/release-1"
        manifest = strict_json((release / "manifest.payload.json").read_bytes())
        package = next(p for p in manifest["packages"] if p["kind"] == "snapshot" and p["platform"] == "windows-x64")
        block = random.Random(731).randbytes(1024*1024)
        digest = hashlib.sha256()
        archive = temp / "large.zip"
        with zipfile.ZipFile(release / (package["id"]+".zip")) as original, zipfile.ZipFile(archive, "w", zipfile.ZIP_STORED) as target:
            for entry in original.infolist():
                if entry.filename != "Data/base.pak":
                    target.writestr(entry, original.read(entry))
                    continue
                entry.compress_type = zipfile.ZIP_STORED
                with target.open(entry, "w", force_zip64=True) as large:
                    for _ in range(64):
                        large.write(block)
                        digest.update(block)
        for records in (package["files"], manifest["platforms"]["windows-x64"]["files"]):
            record = next(f for f in records if f["path"] == "Data/base.pak")
            record.update(size=64*1024**2, sha256=digest.hexdigest())
        package["unpacked_size"] = sum(f["size"] for f in package["files"])
        with archive.open("rb") as stream:
            package.update(size=archive.stat().st_size, sha256=hashlib.file_digest(stream, "sha256").hexdigest())
        envelope = temp / "envelope.json"
        envelope.write_bytes(sign_payload(stable_json(manifest), key, "fixture-only"))
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass
            def do_GET(self):
                self.send_response(200)
                self.send_header("Content-Length", str(package["size"]))
                self.end_headers()
                with archive.open("rb") as stream:
                    while data := stream.read(256*1024):
                        self.wfile.write(data)
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        process = None
        handle = None
        peak = 0
        try:
            started = time.monotonic()
            process = subprocess.Popen([str(driver), "download", str(envelope), key.public_key().public_bytes_raw().hex(),
                package["id"], str(temp / "cache"), str(temp / "stage"), f"http://127.0.0.1:{server.server_port}/large.zip", "0", "-"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=dict(os.environ, NO_PROXY="127.0.0.1,localhost"))
            if os.name == "nt":
                from windows_performance import Counters
                kernel, psapi = ctypes.WinDLL("kernel32"), ctypes.WinDLL("psapi")
                kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
                kernel.OpenProcess.restype = w.HANDLE
                kernel.CloseHandle.argtypes = [w.HANDLE]
                psapi.GetProcessMemoryInfo.argtypes = [w.HANDLE, ctypes.POINTER(Counters), w.DWORD]
                handle = kernel.OpenProcess(0x410, False, process.pid)
                assert handle
            while process.poll() is None:
                if time.monotonic()-started > 45:
                    raise TimeoutError("Native stream benchmark timed out")
                if os.name == "nt":
                    counters = Counters()
                    counters.cb = ctypes.sizeof(counters)
                    if psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
                        peak = max(peak, counters.PeakWorkingSetSize)
                else:
                    try:
                        for line in Path(f"/proc/{process.pid}/status").read_text().splitlines():
                            if line.startswith("VmHWM:"):
                                peak = max(peak, int(line.split()[1])*1024)
                    except FileNotFoundError:
                        pass
                time.sleep(.005)
            stdout, stderr = process.communicate()
            assert process.returncode == 0, (stdout, stderr)
            assert peak > 0
            report = {"platform": sys.platform, "download_bytes": package["size"], "elapsed_download_and_hash_seconds": round(time.monotonic()-started, 3),
                      "native_peak_working_set_MiB": round(peak/1024**2, 3),
                      "scope": "Native core loopback HTTP transfer and SHA-256 of a signed 64 MiB ZIP; excludes Python server/publisher and GUI."}
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
            print(json.dumps(report))
        finally:
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            if handle:
                kernel.CloseHandle(handle)
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)


if __name__ == "__main__":
    main()
