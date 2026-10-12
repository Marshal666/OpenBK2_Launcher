"""Native window smoke test; launches only the supplied fixture executable."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import sys
import time


def main():
    if sys.platform != "win32":
        raise SystemExit("This test requires a native Windows desktop")
    exe = Path(sys.argv[1]).resolve(strict=True)
    output = Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.SetProcessDPIAware()
    user.FindWindowW.restype = wintypes.HWND
    user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.SendMessageW.restype = ctypes.c_ssize_t
    user.PostMessageW.argtypes = user.SendMessageW.argtypes
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.GetWindowRect.argtypes = user.GetClientRect.argtypes
    user.IsIconic.argtypes = [wintypes.HWND]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.SetForegroundWindow.argtypes = [wintypes.HWND]
    user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    original_cursor = wintypes.POINT()
    user.GetCursorPos(ctypes.byref(original_cursor))
    start = time.monotonic()
    proc = subprocess.Popen([str(exe), "--capture", str(output / "native-window.bmp")], cwd=output)
    try:
        hwnd = None
        while time.monotonic() - start < 15:
            candidate = user.FindWindowW(None, "OpenBK2 Launcher")
            pid = wintypes.DWORD()
            if candidate:
                user.GetWindowThreadProcessId(candidate, ctypes.byref(pid))
            if candidate and pid.value == proc.pid and user.IsWindowVisible(candidate):
                hwnd = candidate
                break
            if proc.poll() is not None:
                raise RuntimeError(f"Launcher exited early: {proc.returncode}")
            time.sleep(0.05)
        assert hwnd, "Visible launcher window was not created"
        visible_ms = round((time.monotonic() - start) * 1000)
        time.sleep(1)
        rect = wintypes.RECT()
        assert user.GetWindowRect(hwnd, ctypes.byref(rect))
        client = wintypes.RECT()
        assert user.GetClientRect(hwnd, ctypes.byref(client))
        scale = (client.right - client.left) / 1000

        def point(x, y):
            return ((int(y) & 0xFFFF) << 16) | (int(x) & 0xFFFF)

        # Native WM_NCHITTEST must expose caption dragging only in the blank area.
        assert user.SendMessageW(hwnd, 0x84, 0, point(rect.left + 300*scale, rect.top + 24*scale)) == 2
        assert user.SendMessageW(hwnd, 0x84, 0, point(rect.right - 90*scale, rect.top + 24*scale)) == 1

        def click(x, y):
            user.SetForegroundWindow(hwnd)
            screen = wintypes.POINT(int(x), int(y))
            user.ClientToScreen(hwnd, ctypes.byref(screen))
            moved = user.SetCursorPos(screen.x, screen.y)
            if not moved:
                raise PermissionError("Desktop input is unavailable (SetCursorPos denied); button test not run")
            time.sleep(0.15)
            actual = wintypes.POINT()
            user.GetCursorPos(ctypes.byref(actual))
            assert (actual.x, actual.y) == (screen.x, screen.y), "Desktop input moved unexpectedly"
            user.mouse_event(0x0002, 0, 0, 0, 0)
            time.sleep(0.15)
            user.mouse_event(0x0004, 0, 0, 0, 0)
            time.sleep(0.25)

        click(client.right - 94*scale, 24*scale)
        assert user.IsIconic(hwnd), "Custom minimize button did not minimize the native window"
        user.ShowWindow(hwnd, 9)
        time.sleep(0.3)
        click(client.right - 43*scale, 24*scale)
        assert proc.wait(timeout=10) == 0, "Custom close button did not exit cleanly"
        assert (output / "native-window.bmp").stat().st_size > 1000
        result = {"visible_window_ms": visible_ms, "native_hit_test": "pass",
                  "minimize_button": "pass", "close_button": "pass", "capture": "native-window.bmp"}
        (output / "windows-shell.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result))
    finally:
        user.SetCursorPos(original_cursor.x, original_cursor.y)
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=10)


if __name__ == "__main__":
    main()
