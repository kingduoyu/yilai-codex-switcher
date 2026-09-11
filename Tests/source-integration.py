"""Exercise source cleanup with real config/read, without model requests."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib

repo = Path(__file__).resolve().parent.parent
driver = str(Path(sys.argv[1]).resolve())
runtime = '--auto-runtime' if sys.argv[2] == '--auto-runtime' else str(Path(sys.argv[2]).resolve())
root = Path(tempfile.mkdtemp(prefix="source-test-", dir=repo / "dist")).resolve()
facts = []

def setup(name, trusted=True, nested=False):
    base = root / name
    home = base / 'home'
    project = base / 'project'
    home.mkdir(parents=True)
    (project / '.codex').mkdir(parents=True)
    (project / '.git').mkdir()
    trust_key = str(project).lower() if os.name == 'nt' else str(project)
    user = ('model="gpt-6-astra"\nmodel_provider="custom"\nmodel_reasoning_effort="max"\n'
            '[model_providers.custom]\nname="old-user"\nbase_url="https://old.invalid"\n'
            'wire_api="responses"\nrequires_openai_auth=false\nexperimental_bearer_token="synthetic-user-secret"\n'
            '[model_providers.legacy]\nname="old-project"\nbase_url="https://legacy.invalid"\n'
            'wire_api="responses"\nrequires_openai_auth=false\nexperimental_bearer_token="synthetic-project-secret"\n'
            '[projects.' + json.dumps(trust_key) + ']\ntrust_level="' + ('trusted' if trusted else 'untrusted') + '"\n')
    (home / 'config.toml').write_text(user, encoding='utf8')
    (home / 'auth.json').write_text('{"OPENAI_API_KEY":"synthetic-auth-secret"}', encoding='utf8')
    source = project / '.codex' / 'config.toml'
    source.write_text('model_provider="legacy"\nmodel_reasoning_effort="high"\n[features]\nimage_generation=false\nweb_search_request=true\n[model_providers.custom]\nbase_url="https://shadow.invalid"\nexperimental_bearer_token="synthetic-shadow-secret"\n', encoding='utf8')
    current = project
    if nested:
        current = project / 'nested'
        (current / '.codex').mkdir(parents=True)
        with (home / 'config.toml').open('a', encoding='utf8') as handle:
            handle.write('[projects.' + json.dumps(str(current).lower() if os.name == 'nt' else str(current)) + ']\ntrust_level="trusted"\n')
        (current / '.codex' / 'config.toml').write_text('model_provider="legacy"\n[features]\nimage_generation=false\n', encoding='utf8')
    (home / '.codex-global-state.json').write_text(json.dumps({'active-workspace-roots':[str(current)]}),encoding='utf8')
    (home / 'unused.config.toml').write_text('model_provider="do-not-touch-unused-profile"\n', encoding='utf8')
    return home, source, current

def run(home, action='configure', success=True):
    process = subprocess.run([driver,action,str(home),runtime], capture_output=True, encoding='utf8', timeout=100,
        env=dict(os.environ,CODEX_HOME=str(home),CODEX_SQLITE_HOME=str(home)))
    if success:
        assert process.returncode == 0, process.stderr
    else:
        assert process.returncode != 0, 'Expected a rejected operation'
    logs='\n'.join(p.read_text(encoding='utf8') for p in (home/'yilai-switcher-logs').glob('*.log'))
    for secret in ['synthetic-user-secret','synthetic-project-secret','synthetic-auth-secret','synthetic-shadow-secret','sk-isolated-test-only']:
        assert secret not in logs and secret not in process.stderr, 'Source diagnostics exposed a credential'
    return process,logs

try:
    fresh=root/'fresh-home';fresh.mkdir()
    run(fresh)
    run(fresh,'official')
    facts.append('first-use empty user directory supports API and official switching with effective verification')
    home,source,current=setup('trusted',nested=True)
    unused=(home/'unused.config.toml').read_bytes()
    run(home)
    cleared=tomllib.loads(source.read_text(encoding='utf8'))
    assert 'model_provider' not in cleared and 'image_generation' not in cleared.get('features',{})
    assert cleared['model_reasoning_effort']=='high' and cleared['features']['web_search_request'] is True
    assert 'custom' not in cleared.get('model_providers',{})
    assert 'model_provider' not in tomllib.loads((current/'.codex/config.toml').read_text(encoding='utf8'))
    assert (home/'unused.config.toml').read_bytes()==unused
    assert not (home/'auth.json').exists()
    assert (home/'yilai-source-backups').exists()
    run(home,'official')
    facts.append('trusted parent and child overrides removed; unrelated settings and unused profile retained; API/official actual values verified')

    home,source,current=setup('untrusted',trusted=False)
    before=source.read_bytes()
    run(home)
    assert source.read_bytes()==before
    facts.append('untrusted project layer remains byte-identical and is not treated as active')

    home,source,current=setup('rollback')
    before=source.read_bytes();config=(home/'config.toml').read_bytes();auth=(home/'auth.json').read_bytes()
    (home/'sessions').mkdir();(home/'sessions/broken.jsonl').write_text('malformed-history',encoding='utf8')
    failed,logs=run(home,success=False)
    assert source.read_bytes()==before and (home/'config.toml').read_bytes()==config and (home/'auth.json').read_bytes()==auth
    assert 'verify_sources' in logs and 'rollback_sources' in logs
    facts.append('history failure restores project override, user config and auth after successful source verification')

    home,source,current=setup('invalid-project')
    source.write_text('[invalid TOML',encoding='utf8')
    config=(home/'config.toml').read_bytes();auth=(home/'auth.json').read_bytes()
    run(home,success=False)
    assert source.read_text(encoding='utf8')=='[invalid TOML' and (home/'config.toml').read_bytes()==config and (home/'auth.json').read_bytes()==auth
    facts.append('unreadable effective source blocks before configuration/auth mutation')
    result={'status':'passed','root':str(root),'passed':facts}
    (root/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(result,indent=2))
except Exception as error:
    (root/'failure.json').write_text(json.dumps({'error':str(error),'passed':facts},indent=2),encoding='utf8')
    raise
