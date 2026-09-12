"""Isolated migration and API/official routing regression; no real user data."""
from pathlib import Path
import tempfile, sqlite3, json, subprocess, time, tomllib
exe=Path('dist/test-driver.exe').resolve()
with tempfile.TemporaryDirectory(prefix='yilai-unified-') as tmp:
    root=Path(tmp)
    home=root/'home';home.mkdir()
    config=home/'config.toml';config.write_text("model='gpt-6-astra'\n",encoding='utf-8')
    db=sqlite3.connect(home/'state_5.sqlite')
    db.execute('create table threads(id text primary key,model_provider text,title text,archived integer)')
    tails={}
    for i,provider in enumerate(['yilai','ccswitch','openai','custom','unrelated-private']):
        directory=home/('archived_sessions' if i==1 else 'sessions');directory.mkdir(exist_ok=True)
        head=json.dumps({'type':'session_meta','payload':{'id':str(i),'model_provider':provider}})
        tail='\n'+json.dumps({'type':'session_meta','payload':{'id':'parent','model_provider':'parent-keep'}})+'\n'+json.dumps({'type':'response_item','payload':{'text':'keep messages'}})+'\n'
        file=directory/(str(i)+'.jsonl');file.write_text(head+tail,encoding='utf-8');tails[file]=tail
        db.execute('insert into threads values(?,?,?,?)',(str(i),provider,'title-'+str(i),int(i==1)))
    db.commit();db.close()
    def run(action):
        result=subprocess.run([str(exe),action,str(home)],capture_output=True,timeout=30)
        assert result.returncode==0,result.stderr.decode(errors='replace')
    original=config.read_bytes();start=time.perf_counter();run('unify');elapsed=time.perf_counter()-start
    assert config.read_bytes()==original
    for file,tail in tails.items():
        content=file.read_text(encoding='utf-8');assert content[content.index('\n'):]==tail
        expected='unrelated-private' if file.stem=='4' else 'custom'
        assert json.loads(content.splitlines()[0])['payload']['model_provider']==expected
    db=sqlite3.connect(home/'state_5.sqlite');rows=db.execute('select * from threads order by id').fetchall();db.close()
    assert all(row[1]==('unrelated-private' if row[0]=='4' else 'custom') and row[2]=='title-'+row[0] for row in rows)
    backups=list((home/'yilai-history-backups').glob('*/manifest.json'));assert backups
    run('unify');assert len(list((home/'yilai-history-backups').glob('*/manifest.json')))==len(backups)
    run('configure')
    for action in ['official','configure','official']:
        run(action);c=tomllib.loads(config.read_text(encoding='utf-8'));assert c['model_provider']=='custom'
        for key in ['custom','yilai']:
            v=c['model_providers'][key]
            if action=='official':
                assert v['requires_openai_auth'] and 'base_url' not in v and 'experimental_bearer_token' not in v and 'http_headers' not in v
            else:assert v['base_url']=='https://api.yilai-ai.com' and v['experimental_bearer_token'] and not v['requires_openai_auth']
    assert not (home/'auth.json').exists()
    print(json.dumps({'migration_seconds':elapsed,'passed':['JSONL + SQLite migration','fork parent/message preservation','unknown provider retained','repeat no new backup','API/official routes share custom','both aliases follow selected route','no third-party credentials on official route']}))
