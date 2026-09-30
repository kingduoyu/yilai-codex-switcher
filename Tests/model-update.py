"""Exercise model-only writes and later API configuration in an isolated home."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

driver = str(Path(sys.argv[1]).resolve())
repo = Path(__file__).resolve().parent.parent
base = json.loads((repo / "model-catalog.json").read_text(encoding="utf-8"))
with tempfile.TemporaryDirectory(prefix="model-update-", dir=repo / "dist") as fixture:
    root = Path(fixture)
    home = root / "home"
    home.mkdir()
    config = 'model="synthetic-future-model"\nmodel_provider="custom"\n[model_providers.custom]\nname="OpenAI"\nrequires_openai_auth=true\n'
    (home / "config.toml").write_text(config, encoding="utf-8")
    (home / "auth.json").write_bytes(b"auth-must-stay-unchanged")
    (home / "sessions").mkdir()
    (home / "sessions" / "history.jsonl").write_bytes(b"history-must-stay-unchanged")
    catalog = home / "yilai-model-catalog.json"
    catalog.write_text(json.dumps(base), encoding="utf-8")
    before = {p: p.read_bytes() for p in home.rglob("*") if p.is_file()}
    added = json.loads(json.dumps(base))
    model = dict(added["models"][0])
    model.update(slug="synthetic-future-model", display_name="Synthetic future model")
    added["models"].append(model)
    source = root / "incoming.json"

    def apply(value, ok=True):
        source.write_text(json.dumps(value), encoding="utf-8")
        result = subprocess.run([driver, "models", str(home), str(source)], capture_output=True, encoding="utf-8")
        assert (result.returncode == 0) == ok, result.stdout + result.stderr

    apply(added)
    for path, data in before.items():
        if path != catalog:
            assert path.read_bytes() == data, f"Model update changed {path.name}"
    assert json.loads(catalog.read_text())["models"][-1]["slug"] == model["slug"]
    updated = catalog.read_bytes()
    apply(added)
    assert catalog.read_bytes() == updated
    apply(base, False)
    assert catalog.read_bytes() == updated, "A stale channel removed a model"
    duplicate = json.loads(json.dumps(added))
    duplicate["models"].append(duplicate["models"][0])
    apply(duplicate, False)
    assert catalog.read_bytes() == updated
    for path, data in before.items():
        if path != catalog:
            assert path.read_bytes() == data
    # Reconfiguration must not reset a newly published model or reinstall old bytes.
    (home / "auth.json").unlink()
    result = subprocess.run([driver, "configure", str(home), "--auto-runtime"], capture_output=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "synthetic-future-model" in (home / "config.toml").read_text(encoding="utf-8")
    assert catalog.read_bytes() == updated
    print("PASS: model-only update preserves auth/config/history; stale/duplicate catalogs rejected; later API configuration retains new model")
