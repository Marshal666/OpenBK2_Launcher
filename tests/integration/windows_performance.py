"""Measure a native visible Windows renderer using only isolated fixture directories."""
import ctypes
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from fixture_builder import build_fixtures
from test_workspace import temporary_workspace


class Counters(ctypes.Structure):
    _fields_ = [("cb", w.DWORD), ("PageFaultCount", w.DWORD)] + [(name, ctypes.c_size_t) for name in
        ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
         "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage")]


def main():
    gui, game = map(lambda p: Path(p).resolve(), sys.argv[1:])
    kernel, user, psapi = ctypes.WinDLL("kernel32"), ctypes.WinDLL("user32"), ctypes.WinDLL("psapi")
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.GetProcessTimes.argtypes = [w.HANDLE] + [ctypes.POINTER(w.FILETIME)]*4
    psapi.GetProcessMemoryInfo.argtypes = [w.HANDLE, ctypes.POINTER(Counters), w.DWORD]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback_type, w.LPARAM]
    def windows(pid):
        found = []
        def callback(hwnd, _):
            candidate = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(candidate))
            if candidate.value == pid and user.IsWindowVisible(hwnd):
                found.append(hwnd)
            return True
        user.EnumWindows(callback_type(callback), 0)
        return found
    def cpu(handle):
        created, ended, k, u = w.FILETIME(), w.FILETIME(), w.FILETIME(), w.FILETIME()
        assert kernel.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(ended), ctypes.byref(k), ctypes.byref(u))
        return ((k.dwHighDateTime << 32) + k.dwLowDateTime + (u.dwHighDateTime << 32) + u.dwLowDateTime) / 10_000_000
    artifacts = Path(__file__).resolve().parents[2] / "build/native-audit"
    artifacts.mkdir(exist_ok=True)
    with temporary_workspace() as temp:
        key = build_fixtures(temp / "fixtures", native_game=game).public_key().public_bytes_raw().hex()
        args = [str(gui), "--fixture-release", str(temp / "fixtures/release-1"), "--fixture-public-key", key,
                "--config-dir", str(temp / "config"), "--root", str(temp / "game")]
        subprocess.run(args + ["--auto-install", "--capture", str(artifacts / "native-installed.bmp")], check=True, timeout=40)
        start = time.monotonic()
        process = subprocess.Popen(args)
        handle = kernel.OpenProcess(0x410, False, process.pid)
        assert handle
        try:
            while not windows(process.pid) and time.monotonic()-start < 15:
                time.sleep(.01)
            window_latency = time.monotonic()-start
            assert windows(process.pid), "Native window never appeared"
            time.sleep(1)
            before_cpu, before_time = cpu(handle), time.monotonic()
            time.sleep(5)
            consumed, elapsed = cpu(handle)-before_cpu, time.monotonic()-before_time
            counters = Counters()
            counters.cb = ctypes.sizeof(counters)
            assert psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
            report = {"visible_window_seconds": round(window_latency, 3), "sample_seconds": round(elapsed, 3),
                "idle_cpu_percent_one_core": round(consumed/elapsed*100, 2), "working_set_MiB": round(counters.WorkingSetSize/1024**2, 2),
                "peak_working_set_MiB": round(counters.PeakWorkingSetSize/1024**2, 2), "binary_bytes": gui.stat().st_size,
                "limitations": "Synthetic small installation; native Windows renderer. No physical mouse interaction or large-game startup benchmark."}
            (artifacts / "windows-performance.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
            print(json.dumps(report))
            for hwnd in windows(process.pid):
                user.PostMessageW(hwnd, 0x10, 0, 0)
            process.wait(timeout=15)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            kernel.CloseHandle(handle)


if __name__ == "__main__":
    main()
