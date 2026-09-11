"""Auto-discovery and error privacy regression; never calls a model."""
from pathlib import Path
import json,os,subprocess,sys,tempfile
probe=Path(sys.argv[1]).resolve()
actual_new=Path(sys.argv[2]).resolve()
actual_old=Path(sys.argv[3]).resolve()
root=Path(tempfile.mkdtemp(prefix='runtime-discovery-',dir=Path(__file__).resolve().parent.parent/'dist'))
bin=root/'LocalAppData/OpenAI/Codex/bin';bin.mkdir(parents=True)
loose=bin/'codex.exe';loose.write_bytes(b'not executed')
older=bin/'old/codex.exe';older.parent.mkdir();older.write_bytes(b'not executed')
current=bin/'current/codex.exe';current.parent.mkdir();current.write_bytes(b'not executed')
os.utime(older,(10,10));os.utime(current,(20,20));os.utime(loose,(30,30))
env=dict(os.environ,LOCALAPPDATA=str(root/'LocalAppData'))
def locate():
 p=subprocess.run([str(probe)],env=env,capture_output=True,encoding='utf8',check=True)
 return Path(json.loads(p.stdout)['runtime'])
assert locate()==current,'Loose legacy runtime shadowed desktop runtime'
current.unlink();assert locate()==older
older.unlink();assert locate()==loose,'Legacy-only install lost fallback'
home=root/'home';home.mkdir()
config='model="gpt-6-astra"\nmodel_provider="custom"\nmodel_reasoning_effort="max"\n[model_providers.custom]\nname="Synthetic"\nbase_url="http://127.0.0.1:1/v1"\nwire_api="responses"\nrequires_openai_auth=false\nexperimental_bearer_token="synthetic-not-a-real-key"\n'
(home/'config.toml').write_text(config,encoding='utf8')
def read(runtime):return subprocess.run([str(probe),str(home),str(home),str(runtime)],capture_output=True,encoding='utf8',timeout=30)
old=read(actual_old);assert old.returncode!=0 and '推理档位' in old.stderr and 'RPC -32603' in old.stderr
assert str(actual_old) in old.stderr and 'synthetic-not-a-real-key' not in old.stderr
new=read(actual_new);assert new.returncode==0,new.stderr
assert json.loads(new.stdout)['reasoning']=='max'
(home/'config.toml').write_text(config.replace('"max"','"private-value-do-not-log"'),encoding='utf8')
private=read(actual_old);assert private.returncode!=0 and 'private-value-do-not-log' not in private.stderr and '推理档位' in private.stderr
result={'status':'passed','cases':['versioned desktop runtime preferred over newer-mtime legacy loose file','latest versioned candidate selected; legacy-only fallback retained','old runtime max rejection has classified error, RPC code and runtime path','current runtime accepts max','unknown setting value and API token omitted from error'],'root':str(root)}
(root/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(result,ensure_ascii=False,indent=2))
