"""Run native macOS update/rollback fixtures: python3 Tests/macos-update.py /absolute/App.app.

Only copied fixtures are changed. Their App Sandbox signatures deny networking
when the native helper opens them through LaunchServices. Requires a macOS GUI
runner, codesign, lipo, ditto and an already-built universal application.
"""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import signal
import subprocess
import sys
import time
import uuid


BUNDLE_ID = "com.yilai.codex-switcher"
EXECUTABLE = "YilaiCodexSwitcherMac"


class TestFailure(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise TestFailure(message)


def run(arguments, timeout=30):
    result = subprocess.run(arguments, stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)
    require(result.returncode == 0,
            f"{Path(arguments[0]).name} failed ({result.returncode}): "
            + result.stderr.decode("utf-8", errors="replace")[:2000])
    return result.stdout


def save_evidence(path, value):
    require(path.parent.is_dir() and not path.is_symlink(), "Invalid evidence file location")
    require(path.resolve(strict=False).parent == path.parent.resolve(strict=True), "Redirected evidence file location")
    temporary = path.with_name(".macos-update-result-" + str(uuid.uuid4()) + ".json")
    require(temporary.resolve(strict=False).parent == path.parent.resolve(strict=True), "Invalid evidence staging path")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True)
        output.write("\n")
    os.replace(temporary, path)


def contained(path, root, allow_root=False):
    root = root.resolve(strict=True)
    resolved = path.resolve(strict=False)
    require(resolved.is_relative_to(root) and (allow_root or resolved != root),
            f"Refusing operation outside fixture root: {path}")
    return resolved


def remove_fixture(path, root, allow_root=False):
    resolved = contained(path, root, allow_root)
    if resolved.is_dir():
        for child in resolved.rglob("*"):
            contained(child, root)
        shutil.rmtree(resolved)
    elif resolved.exists():
        resolved.unlink()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def bundle_sha256(app):
    require(app.is_dir() and not app.is_symlink(), f"Invalid app directory: {app}")
    digest = hashlib.sha256()
    for path in sorted(app.rglob("*")):
        require(not path.is_symlink(), f"Symlink in app fixture input: {path}")
        require(path.is_dir() or path.is_file(), f"Non-regular app entry: {path}")
        if path.is_file():
            digest.update(path.relative_to(app).as_posix().encode("utf-8") + b"\0")
            digest.update(bytes.fromhex(sha256(path)))
    return digest.hexdigest()


class AppProcesses:
    def __init__(self):
        self.libproc = ctypes.CDLL("/usr/lib/libproc.dylib", use_errno=True)
        self.libproc.proc_pidpath.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
        self.libproc.proc_pidpath.restype = ctypes.c_int

    def path(self, pid):
        buffer = ctypes.create_string_buffer(4096)
        if self.libproc.proc_pidpath(pid, buffer, len(buffer)) <= 0:
            return None
        return Path(os.fsdecode(buffer.value)).resolve(strict=False)

    def matching(self, executable):
        expected = executable.resolve(strict=False)
        pids = run(["/bin/ps", "-axo", "pid="], timeout=5).split()
        return [int(pid) for pid in pids if self.path(int(pid)) == expected]

    def wait_for_app(self, executable):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            pids = self.matching(executable)
            if pids:
                return pids
            time.sleep(0.1)
        raise TestFailure(f"LaunchServices did not start the fixture app: {executable}")

    def close_fixture(self, executable, root):
        expected = contained(executable, root)
        pids = self.matching(expected)
        for pid in pids:
            if self.path(pid) == expected:
                try:
                    os.kill(pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and any(self.path(pid) == expected for pid in pids):
            time.sleep(0.05)
        for pid in pids:
            if self.path(pid) == expected:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and self.matching(expected):
            time.sleep(0.05)
        require(not self.matching(expected), f"Fixture app did not stop: {expected}")


def stop_child(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def read_receipt(path):
    require(not path.is_symlink(), "Refusing a symlink at the shared native update receipt")
    if not path.exists():
        return None
    require(path.is_file() and path.stat().st_size <= 64 * 1024,
            "Existing shared update receipt is not a small regular file")
    try:
        data = path.read_bytes()
        return data, json.loads(data)
    except (ValueError, OSError) as error:
        raise TestFailure("Existing shared native update receipt cannot be identified as a fixture") from error


def fixture_receipt(value, backups):
    return (isinstance(value, dict) and isinstance(value.get("success"), bool)
            and isinstance(value.get("pending"), bool) and isinstance(value.get("message"), str)
            and any(str(backup) in value["message"] for backup in backups))


def check_receipt_available(path, backups):
    receipt = read_receipt(path)
    require(receipt is None or fixture_receipt(receipt[1], backups),
            "A non-fixture update receipt exists; refusing to overwrite it")


def cleanup_receipt(path, backups):
    # This exact native receipt is the only cleanup exception outside testroot.
    # An absent, consumed or replaced non-fixture receipt is left untouched.
    receipt = read_receipt(path)
    if receipt is not None and fixture_receipt(receipt[1], backups):
        require(path.resolve(strict=True) == path, "Refusing redirected native receipt cleanup")
        if path.read_bytes() == receipt[0]:
            path.unlink()


def signed_fixture(source, destination, version, role, case_root, entitlements, root):
    contained(destination, root)
    run(["/usr/bin/ditto", str(source), str(destination)])
    plist = contained(destination / "Contents/Info.plist", root)
    info = plistlib.loads(plist.read_bytes())
    info["CFBundleShortVersionString"] = version
    info["CFBundleVersion"] = version
    # open(1) launches via LaunchServices, so its minimal environment cannot
    # propagate the helper's CODEX_HOME to the GUI. Set it in each copied app.
    info["LSEnvironment"] = {
        "CODEX_HOME": str(case_root / "codex-home"),
        "CFFIXED_USER_HOME": str(case_root / "preferences-home"),
        "TMPDIR": str(case_root / "tmp") + "/",
    }
    plist.write_bytes(plistlib.dumps(info))
    marker = contained(destination / "Contents/Resources/update-test-fixture.json", root)
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps({"case": case_root.name, "role": role, "version": version}), encoding="utf-8")
    run(["/usr/bin/codesign", "--force", "--deep", "--sign", "-", "--timestamp=none",
         "--entitlements", str(entitlements), str(destination)])
    run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(destination)])
    actual = plistlib.loads(run(["/usr/bin/codesign", "--display", "--entitlements", ":-", str(destination)]))
    require(actual.get("com.apple.security.app-sandbox") is True
            and not actual.get("com.apple.security.network.client")
            and not actual.get("com.apple.security.network.server"),
            "Fixture signature does not deny network access")
    run(["/usr/bin/lipo", "-verify_arch", "x86_64", "arm64", str(destination / "Contents/MacOS" / EXECUTABLE)])
    return bundle_sha256(destination)


def wait_ready(helper, old, stage, root, log):
    status_file = contained(stage / "helper-status.json", root)
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        if status_file.exists():
            require(not status_file.is_symlink() and status_file.stat().st_size <= 64 * 1024,
                    "Invalid helper readiness file")
            status = json.loads(status_file.read_bytes())
            require(status.get("helperPID") == helper.pid and status.get("parentPID") == old.pid,
                    "Helper readiness PID mismatch")
            require(status.get("ready") is True, "Helper preflight failed: " + str(status.get("message", "")))
            require(helper.poll() is None and old.poll() is None, "Helper or dummy old app exited before readiness")
            return
        if helper.poll() is not None:
            raise TestFailure(f"Helper exited before readiness ({helper.returncode}): "
                              + log.read_text(encoding="utf-8", errors="replace")[:2000])
        time.sleep(0.05)
    raise TestFailure("Native helper readiness timed out; dummy old app was not stopped")


def case(name, source, old_version, new_version, root, receipt, backups, processes, user):
    case_root = contained(root / name, root)
    case_root.mkdir(mode=0o700)
    for directory in ["codex-home", "preferences-home", "tmp"]:
        contained(case_root / directory, root).mkdir(mode=0o700)
    home = case_root / "codex-home"
    sentinels = {"config.toml": b'model_provider="openai"\n', "auth.json": b"synthetic-auth-must-not-change",
                 "history.jsonl": b"synthetic-history-must-not-change\n"}
    for filename, data in sentinels.items():
        contained(home / filename, root).write_bytes(data)
    stage = contained(case_root / (".yilai-update-" + str(uuid.uuid4())), root)
    stage.mkdir(mode=0o700)
    stage.chmod(0o700)
    contained(stage / "unpacked", root).mkdir(mode=0o700)
    destination = contained(case_root / source.name, root)
    candidate = contained(stage / "unpacked" / source.name, root)
    backup = contained(case_root / (destination.stem + ".backup-" + str(uuid.uuid4()) + ".app"), root)
    backups.append(backup)
    check_receipt_available(receipt, backups)
    entitlements = contained(case_root / "fixture-entitlements.plist", root)
    entitlements.write_bytes(plistlib.dumps({"com.apple.security.app-sandbox": True,
                                          "com.apple.security.network.client": False,
                                          "com.apple.security.network.server": False}))
    old_hash = signed_fixture(source, destination, old_version, "old", case_root, entitlements, root)
    new_hash = signed_fixture(source, candidate, new_version, "candidate", case_root, entitlements, root)
    require(old_hash != new_hash, "Old and candidate fixtures must be distinguishable")
    old_binary_hash = sha256(destination / "Contents/MacOS" / EXECUTABLE)
    helper_path = contained(stage / "update-helper", root)
    shutil.copy2(source / "Contents/MacOS" / EXECUTABLE, helper_path)
    helper_path.chmod(0o700)
    helper_hash = sha256(helper_path)
    require(helper_hash == sha256(source / "Contents/MacOS" / EXECUTABLE), "Copied native helper bytes changed")
    environment = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "LC_ALL": "C", "HOME": user.pw_dir,
                   "USER": user.pw_name, "LOGNAME": user.pw_name,
                   "CODEX_HOME": str(home), "TMPDIR": str(case_root / "tmp") + "/"}
    log = contained(case_root / "helper.log", root)
    old = helper = None
    executable = destination / "Contents/MacOS" / EXECUTABLE
    try:
        old = subprocess.Popen(["/bin/sleep", "120"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, env=environment, cwd=case_root)
        with log.open("wb") as output:
            helper = subprocess.Popen([str(helper_path), "--update-helper", str(old.pid), str(destination),
                                       str(candidate), str(backup), str(receipt), new_version, old_version],
                                      stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                                      env=environment, cwd=case_root)
            wait_ready(helper, old, stage, root, log)
            require(bundle_sha256(destination) == old_hash and not backup.exists(),
                    "Native helper changed the installation before the old PID exited")
            if name == "rollback":
                # After validation, remove only this candidate. The helper must
                # back up the destination before encountering candidate ENOENT.
                remove_fixture(candidate, root)
            stop_child(old)  # Reap sleep so kill(pid, 0) observes ESRCH, not a zombie.
            exit_code = helper.wait(timeout=55)
        require((exit_code == 0) == (name == "success"),
                f"Unexpected helper exit ({exit_code}): " + log.read_text(encoding="utf-8", errors="replace")[:2000])
        require(backup.is_dir() and bundle_sha256(backup) == old_hash,
                "Native helper did not retain the complete old app backup")
        require(sha256(backup / "Contents/MacOS" / EXECUTABLE) == old_binary_hash,
                "Backed-up old executable bytes changed")
        expected_hash = new_hash if name == "success" else old_hash
        require(bundle_sha256(destination) == expected_hash,
                "Destination is not the installed candidate" if name == "success" else "Original destination was not restored")
        require(not candidate.exists(), "Candidate was not moved or removed")
        require(stage.exists() == (name == "rollback"), "Unexpected helper staging cleanup result")
        app_pids = processes.wait_for_app(executable)
        processes.close_fixture(executable, root)
        for filename, data in sentinels.items():
            require((home / filename).read_bytes() == data, f"Fixture app changed isolated {filename}")
        result = {"case": name, "helper_exit": exit_code, "helper_sha256": helper_hash,
                  "old_bundle_sha256": old_hash, "candidate_bundle_sha256": new_hash,
                  "destination_sha256": expected_hash, "old_executable_sha256": old_binary_hash,
                  "backup_retained": True, "fixture_app_pids": app_pids, "network_denied_by_app_sandbox": True}
        print(json.dumps(result, sort_keys=True))
        return result
    finally:
        # Stop the helper before releasing its dummy PID on an unsuccessful test.
        stop_child(helper)
        stop_child(old)
        processes.close_fixture(executable, root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Absolute path to the built, signed universal .app")
    args = parser.parse_args()
    require(sys.platform == "darwin", "macos-update.py requires a macOS GUI runner")
    require(os.environ.get("CI", "").lower() == "true", "Run this fixture only on a macOS CI GUI runner")
    gui = subprocess.run(["/bin/launchctl", "print", f"gui/{os.getuid()}"], stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
    require(gui.returncode == 0, "The CI user has no LaunchServices GUI session")
    require(args.app.is_absolute(), "APP must be an absolute path")
    source = args.app.resolve(strict=True)
    require(source.suffix == ".app", "APP must be an application bundle")
    info = plistlib.loads((source / "Contents/Info.plist").read_bytes())
    require(info.get("CFBundleIdentifier") == BUNDLE_ID and info.get("CFBundleExecutable") == EXECUTABLE,
            "APP is not the expected Yilai application")
    old_version = info.get("CFBundleShortVersionString", "")
    require(re.fullmatch(r"[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}", old_version) is not None, "Invalid APP version")
    parts = [int(part) for part in old_version.split(".")]
    require(parts[2] < 9999, "Fixture version patch cannot be incremented")
    new_version = f"{parts[0]}.{parts[1]}.{parts[2] + 1}"
    run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(source)])
    run(["/usr/bin/lipo", "-verify_arch", "x86_64", "arm64", str(source / "Contents/MacOS" / EXECUTABLE)])
    original_hash = bundle_sha256(source)
    repo = Path(__file__).resolve().parent.parent
    dist = repo / "dist"
    require(not dist.is_symlink(), "Refusing a symlink at repo/dist")
    dist.mkdir(exist_ok=True)
    root = (dist / ("macos-update-" + str(uuid.uuid4()))).resolve(strict=False)
    require(root.parent == dist.resolve(strict=True) and not source.is_relative_to(root), "Invalid test root")
    root.mkdir(mode=0o700)
    import pwd
    user = pwd.getpwuid(os.getuid())
    receipt = Path(user.pw_dir).resolve(strict=True) / "Library/Application Support" / BUNDLE_ID / "update-result.json"
    evidence_path = dist.resolve(strict=True) / "macos-update-result.json"
    evidence = {"schema_version": 1, "passed": False, "app": str(source), "source_bundle_sha256": original_hash,
                "old_version": old_version, "candidate_version": new_version, "fixture_root": str(root),
                "fixtures_retained": True, "native_result_path": str(receipt), "cases": []}
    save_evidence(evidence_path, evidence)
    backups = []
    try:
        require(receipt.resolve(strict=False) == receipt, "Native receipt directory is redirected")
        check_receipt_available(receipt, backups)
        processes = AppProcesses()
        for name in ["success", "rollback"]:
            record = {"case": name, "passed": False}
            evidence["cases"].append(record)
            record.update(case(name, source, old_version, new_version, root, receipt, backups, processes, user))
            record["passed"] = True
            save_evidence(evidence_path, evidence)
        require(bundle_sha256(source) == original_hash, "The supplied APP was modified")
        evidence["source_app_unchanged"] = True
        cleanup_receipt(receipt, backups)
        remove_fixture(root, root, allow_root=True)
        evidence["fixtures_retained"] = False
        evidence["passed"] = True
        save_evidence(evidence_path, evidence)
        print(f"Evidence: {evidence_path}")
        print("PASS: native macOS helper installs/relaunches, retains backup, and restores after candidate ENOENT; source APP and isolated Codex data unchanged")
    except Exception as error:
        evidence["failure"] = str(error)
        evidence["source_app_unchanged"] = bundle_sha256(source) == original_hash
        save_evidence(evidence_path, evidence)
        print(f"Evidence: {evidence_path}", file=sys.stderr)
        print(f"FAILED: fixtures retained at {root}", file=sys.stderr)
        raise


if __name__ == "__main__":
    try:
        main()
    except (TestFailure, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
