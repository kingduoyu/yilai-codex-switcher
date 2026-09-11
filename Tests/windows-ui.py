"""Exercise the real Windows failure transition in an isolated Codex home."""
import ctypes as c
import ctypes.wintypes as w
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

u = c.WinDLL("user32", use_last_error=True)
callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [callback_type, w.LPARAM]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = w.LPARAM
u.IsWindowEnabled.argtypes = [w.HWND]
u.GetWindowLongW.argtypes = [w.HWND, c.c_int]
u.GetWindowLongW.restype = c.c_long

binary = Path(sys.argv[1] if len(sys.argv) > 1 else "dist/windows/YilaiCodexSwitcher.exe").resolve()
with tempfile.TemporaryDirectory(prefix="yilai-ui-regression-") as fixture:
    home = Path(fixture)
    original = "[broken"
    (home / "config.toml").write_text(original, encoding="utf8")
    environment = dict(os.environ, CODEX_HOME=str(home), CODEX_SQLITE_HOME=str(home))
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(binary)], env=environment, startupinfo=startup)
    found = []
    @callback_type
    def visit(hwnd, unused):
        pid = w.DWORD()
        u.GetWindowThreadProcessId(hwnd, c.byref(pid))
        if pid.value == process.pid and u.GetDlgItem(hwnd, 1002):
            found.append(hwnd)
        return True
    try:
        for _ in range(100):
            u.EnumWindows(visit, 0)
            if found:
                break
            time.sleep(0.1)
        assert found, "Application window did not start"
        window = found[0]
        logs = u.GetDlgItem(window, 1005)
        official = u.GetDlgItem(window, 1002)
        assert logs and official, "Expected controls missing"
        for attempt in range(2):
            u.SendMessageW(window, 0x111, 1002, official)
            for _ in range(150):
                if u.GetWindowLongW(logs, -16) & 0x10000000 and u.IsWindowEnabled(official):
                    break
                time.sleep(0.1)
            assert u.GetWindowLongW(logs, -16) & 0x10000000, "Failure did not reveal logs"
            assert u.IsWindowEnabled(logs), "Visible log button is disabled after failure"
            assert u.IsWindowEnabled(official), "Switch button did not recover"
            assert (home / "config.toml").read_text(encoding="utf8") == original
        entries = [json.loads(line) for log in (home / "yilai-switcher-logs").glob("*.log")
                   for line in log.read_text(encoding="utf8").splitlines()]
        assert sum(row.get("result") == "failure" for row in entries) == 2
        print("PASS: two real GUI failures expose enabled logs, preserve config, and write failure records")
    finally:
        if found:
            u.SendMessageW(found[0], 0x10, 0, 0)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)
