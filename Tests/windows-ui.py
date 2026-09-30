"""Exercise the real Windows failure transition in an isolated Codex home."""
import ctypes as c
import ctypes.wintypes as w
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import json
import re

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
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]

binary = Path(sys.argv[1] if len(sys.argv) > 1 else "dist/windows/YilaiCodexSwitcher.exe").resolve()
with tempfile.TemporaryDirectory(prefix="yilai-ui-regression-") as fixture:
    home = Path(fixture)
    original = "[broken"
    (home / "config.toml").write_text(original, encoding="utf8")
    environment = dict(os.environ, CODEX_HOME=str(home), CODEX_SQLITE_HOME=str(home), YILAI_SKIP_UPDATE_CHECK="1", YILAI_UI_TEST="1")
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(binary)], env=environment, startupinfo=startup)
    found = []
    @callback_type
    def visit(hwnd, unused):
        pid = w.DWORD()
        u.GetWindowThreadProcessId(hwnd, c.byref(pid))
        if pid.value == process.pid and u.GetDlgItem(hwnd, 1001):
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
        api = u.GetDlgItem(window, 1001)
        assert api, "Expected controls missing"
        assert not u.GetDlgItem(window, 1005), "Process log button remains"
        assert u.GetDlgItem(window, 1002), "Official button missing"
        assert not u.GetDlgItem(window, 1006), "Removed history repair button remains"
        models = u.GetDlgItem(window, 1010)
        updater = u.GetDlgItem(window, 1007)
        assert models and updater and u.GetDlgItem(window, 1008), "Update controls missing"
        catalog = json.loads((Path(__file__).resolve().parents[1] / "model-catalog.json").read_text(encoding="utf8"))
        assert u.SendMessageW(models, 0x0146, 0, 0) == len(catalog["models"]) + 1, "Model dropdown is incomplete"
        for index, model in enumerate(catalog["models"], 1):
            label = c.create_unicode_buffer(256)
            u.SendMessageW(models, 0x0148, index, c.cast(label, c.c_void_p).value)
            assert label.value == model["display_name"], "Model name is duplicated or missing"
        version = c.create_unicode_buffer(100)
        u.GetWindowTextW(updater, version, len(version))
        assert re.fullmatch(r"v\d+\.\d+\.\d+", version.value), "Version label is not a single compact value"
        model_rect, api_rect, update_rect = w.RECT(), w.RECT(), w.RECT()
        u.GetWindowRect(models, c.byref(model_rect))
        u.GetWindowRect(api, c.byref(api_rect))
        u.GetWindowRect(updater, c.byref(update_rect))
        assert model_rect.bottom <= api_rect.top, "Model list overlaps API button"
        assert update_rect.bottom < model_rect.top, "Software update is not in header"
        u.FindWindowExW.argtypes = [w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR]
        u.FindWindowExW.restype = w.HWND
        edit = u.FindWindowExW(window, None, "Edit", None)
        assert edit, "API input missing"
        key = c.c_wchar_p("synthetic-ui-only")
        u.SendMessageW(edit, 0x000C, 0, c.cast(key, c.c_void_p).value)
        for attempt in range(2):
            u.SendMessageW(window, 0x111, 1001, api)
            for _ in range(150):
                if u.IsWindowEnabled(api):
                    break
                time.sleep(0.1)
            assert u.IsWindowEnabled(api), "Switch button did not recover"
            assert (home / "config.toml").read_text(encoding="utf8") == original
        assert not (home / "yilai-switcher-logs").exists()
        print("PASS: single-name model list, compact header version, no history repair, and two GUI failures recover without changing config")
    finally:
        if found:
            u.SendMessageW(found[0], 0x10, 0, 0)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)
