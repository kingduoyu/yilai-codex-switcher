import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { mkdtemp, mkdir, readFile, writeFile, readdir, access } from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { fileURLToPath } from 'node:url';
const [driverArg, codex, templatePath] = process.argv.slice(2);
const driver = driverArg && path.resolve(driverArg);
assert(driver && codex && templatePath, 'Usage: node Tests/runtime-integration.mjs DRIVER CODEX CCS_TEMPLATE');
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const root = await mkdtemp(path.join(repo, 'dist/runtime-test-'));
const home = path.join(root, 'home'); await mkdir(home);
const requests = [];
const mock = http.createServer(async (req,res) => {
 let raw=''; for await (const chunk of req) raw+=chunk;
 let body; try { body=JSON.parse(raw); } catch { body=raw; }
 requests.push({url:req.url,headers:req.headers,body});
 if (!req.url.endsWith('/responses')) {res.writeHead(404);res.end('{}');return;}
 res.writeHead(200,{'Content-Type':'text/event-stream'});
 const id='resp_mock_'+requests.length;
 const item={type:'message',id:'msg_'+requests.length,role:'assistant',status:'completed',content:[{type:'output_text',text:'Synthetic reply '+requests.length,annotations:[]}]};
 const emit=(type,data)=>res.write('event: '+type+'\ndata: '+JSON.stringify({type,...data})+'\n\n');
 emit('response.created',{response:{id,object:'response',status:'in_progress',output:[]}});
 emit('response.output_item.added',{output_index:0,item:{...item,status:'in_progress',content:[]}});
 emit('response.content_part.added',{item_id:item.id,output_index:0,content_index:0,part:{type:'output_text',text:'',annotations:[]}});
 emit('response.output_text.delta',{item_id:item.id,output_index:0,content_index:0,delta:item.content[0].text});
 emit('response.output_text.done',{item_id:item.id,output_index:0,content_index:0,text:item.content[0].text});
 emit('response.output_item.done',{output_index:0,item});
 emit('response.completed',{response:{id,object:'response',status:'completed',output:[item],usage:{input_tokens:10,output_tokens:5,total_tokens:15}}});
 res.end();
});
await new Promise(resolve=>mock.listen(0,'127.0.0.1',resolve));
const port=mock.address().port;
const templateText=await readFile(templatePath,'utf8');
const catalog={models:['gpt-5.6-sol','gpt-5.6-terra','gpt-6-astra'].map(slug=>({...JSON.parse(templateText),slug,display_name:slug}))};
const catalogPath=path.join(home,'cc-switch-model-catalog.json');
await writeFile(catalogPath,JSON.stringify(catalog,null,2));
const config="model='gpt-6-astra'\nmodel_provider='custom'\nmodel_catalog_json="+JSON.stringify(catalogPath.replaceAll('\\','/'))+"\ncli_auth_credentials_store='file'\napproval_policy='never'\nsandbox_mode='read-only'\n[analytics]\nenabled=false\n[model_providers.custom]\nname='CCS synthetic'\nbase_url='http://127.0.0.1:"+port+"/v1'\nwire_api='responses'\nrequires_openai_auth=false\nexperimental_bearer_token='sk-synthetic-local-only'\n[model_providers.custom.http_headers]\nX-Keep='unchanged'\n";
const auth=JSON.stringify({OPENAI_API_KEY:'sk-fake-file-credential'});
await writeFile(path.join(home,'config.toml'),config);await writeFile(path.join(home,'auth.json'),auth);
function operation(action,probe=false){const r=spawnSync(driver,[action,home,...(probe?[codex]:[])],{encoding:'utf8',windowsHide:true,env:{...process.env,CODEX_HOME:home,CODEX_SQLITE_HOME:home}});assert.equal(r.status,0,r.stderr);}
class Server {
 constructor(){this.pending=new Map();this.notifications=[];this.id=0;this.stderr='';this.buffer='';this.child=spawn(codex,['app-server'],{cwd:home,env:{...process.env,CODEX_HOME:home,CODEX_SQLITE_HOME:home},windowsHide:true,stdio:['pipe','pipe','pipe']});this.done=new Promise(resolve=>this.child.on('close',resolve));this.child.stderr.on('data',x=>this.stderr+=x);this.child.stdout.on('data',x=>{this.buffer+=x;let i;while((i=this.buffer.indexOf('\n'))>=0){const line=this.buffer.slice(0,i);this.buffer=this.buffer.slice(i+1);let msg;try{msg=JSON.parse(line);}catch{continue;}if(this.pending.has(msg.id)){const w=this.pending.get(msg.id);this.pending.delete(msg.id);clearTimeout(w.timer);msg.error?w.reject(new Error(JSON.stringify(msg.error))):w.resolve(msg.result);}else{this.notifications.push(msg);}}});this.child.on('exit',code=>{for(const w of this.pending.values()){clearTimeout(w.timer);w.reject(new Error('Runtime exit '+code+' '+this.stderr));}this.pending.clear();});}
 request(method,params){return new Promise((resolve,reject)=>{const id=++this.id;const timer=setTimeout(()=>{this.pending.delete(id);reject(new Error('Timeout '+method+' '+this.stderr));},25000);this.pending.set(id,{resolve,reject,timer});this.child.stdin.write(JSON.stringify({id,method,params})+'\n');});}
 async init(){await this.request('initialize',{clientInfo:{name:'rewrite_integration',version:'3.3.8'},capabilities:{experimentalApi:true}});this.child.stdin.write(JSON.stringify({method:'initialized',params:{}})+'\n');return this;}
 async turn(id,text){const result=await this.request('turn/start',{threadId:id,input:[{type:'text',text,text_elements:[]}]});const deadline=Date.now()+25000;while(Date.now()<deadline){const n=this.notifications.find(n=>n.method==='turn/completed'&&n.params.threadId===id&&n.params.turn.id===result.turn.id);if(n){assert.equal(n.params.turn.status,'completed',JSON.stringify(n));return n;}await new Promise(r=>setTimeout(r,30));}throw new Error('Turn timeout '+this.stderr+' '+JSON.stringify(this.notifications));}
 async close(){this.child.stdin.end();this.child.kill();await this.done;}
}
const facts={root,runtime:spawnSync(codex,['--version'],{encoding:'utf8',windowsHide:true}).stdout.trim()};
let server;
async function snapshots(){
 const saved={};
 async function visit(dir){for(const entry of await readdir(dir,{withFileTypes:true})){const p=path.join(dir,entry.name);if(entry.isDirectory())await visit(p);else saved[p]=(await readFile(p)).toString('base64');}}
 for(const name of ['sessions','archived_sessions','yilai-history-backups']){try{await visit(path.join(home,name));}catch(e){if(e.code!=='ENOENT')throw e;}}
 for(const name of await readdir(home)){if(!name.startsWith('state_5.sqlite') && /\.sqlite(?:-wal|-shm)?$/.test(name))saved[path.join(home,name)]=(await readFile(path.join(home,name))).toString('base64');}
 const logical=spawnSync('python',['-c',"import sqlite3,json,sys; c=sqlite3.connect('file:'+sys.argv[1]+'?mode=ro',uri=True); print(json.dumps(c.execute('select * from threads order by id').fetchall(),sort_keys=True))",path.join(home,'state_5.sqlite')],{encoding:'utf8',windowsHide:true});
 assert.equal(logical.status,0,logical.stderr);saved['logical-thread-index']=logical.stdout;
 return saved;
}
try{
 const originalCatalog=await readFile(catalogPath,'utf8');
 operation('configure');await assert.rejects(access(path.join(home,'auth.json')));
 const configured=await readFile(path.join(home,'config.toml'),'utf8');
 operation('configure');assert.equal(await readFile(path.join(home,'config.toml'),'utf8'),configured);
 assert.equal(await readFile(catalogPath,'utf8'),originalCatalog);
 // Only the synthetic test changes the endpoint; the application keeps its production endpoint.
 await writeFile(path.join(home,'config.toml'),configured.replace('https://api.yilai-ai.com','http://127.0.0.1:'+port+'/v1'));
 server=await new Server().init();
 const models=await server.request('model/list',{includeHidden:false,limit:100});
 facts.models=models.data.map(m=>m.model??m.id);
 assert.deepEqual([...facts.models].sort(),['gpt-5.6-sol','gpt-5.6-terra','gpt-6-astra'].sort());
 for(const id of ['gpt-5.6-sol','gpt-5.6-terra','gpt-6-astra'])assert(facts.models.includes(id));
 const effective=(await server.request('config/read',{includeLayers:false})).config;
 assert.equal(effective.model,'gpt-6-astra');assert.equal(effective.features.image_generation,true);
 assert.equal(effective.model_catalog_json.replaceAll('\\','/'),path.join(home,'yilai-model-catalog.json').replaceAll('\\','/'));
 const login=await server.request('account/read',{refreshToken:false});assert.equal(login.requiresOpenaiAuth,false);assert.equal(login.account,null);facts.apiWithoutOfficialLogin=login;
 const t=await server.request('thread/start',{cwd:home,model:'gpt-6-astra',modelProvider:'custom',approvalPolicy:'never',sandbox:'read-only'});
 const id=t.thread.id;facts.threadId=id;await server.turn(id,'Synthetic API configuration verification');
 const detail=await server.request('thread/read',{threadId:id,includeTurns:true});assert(JSON.stringify(detail).includes('Synthetic reply'));
 await server.close();server=null;
 const responses=requests.filter(r=>r.url.endsWith('/responses'));assert(responses.length>=1);
 for(const r of responses){assert.equal(r.headers.authorization,'Bearer sk-isolated-test-only');assert.equal(r.headers['x-openai-actor-authorization'],'local-image-extension');assert.equal(r.body.model,'gpt-6-astra');assert(r.body.tools.some(t=>t.type==='namespace'&&t.name==='image_gen'&&t.tools.some(f=>f.name==='imagegen')));}
 // Recreate the reported legacy provider in a real runtime-created rollout and index.
 const alter = spawnSync('python',['-c',`import sqlite3,json,pathlib,sys
h=pathlib.Path(sys.argv[1]);tid=sys.argv[2];db=sqlite3.connect(h/'state_5.sqlite')
p=pathlib.Path(db.execute('select rollout_path from threads where id=?',(tid,)).fetchone()[0])
b=p.read_bytes();head,tail=b.split(b'\\n',1);m=json.loads(head);m['payload']['model_provider']='yilai';p.write_bytes(json.dumps(m).encode()+b'\\n'+tail)
db.execute('update threads set model_provider=? where id=?',('yilai',tid));db.commit();db.close()`,home,id],{encoding:'utf8',windowsHide:true});
 assert.equal(alter.status,0,alter.stderr);
 operation('official');
 server=await new Server().init();
 const official=(await server.request('config/read',{includeLayers:false})).config;
 assert.equal(official.model_provider,'custom');
 assert.equal(official.model_providers.custom.requires_openai_auth,true);
 assert(!official.model_providers.custom.base_url);
 assert(!official.model_providers.custom.experimental_bearer_token);
 const officialThread=await server.request('thread/read',{threadId:id,includeTurns:true});
 assert.equal(officialThread.thread.modelProvider,'custom');
 await server.close();server=null;
 operation('configure');
 await writeFile(path.join(home,'config.toml'),configured.replaceAll('https://api.yilai-ai.com','http://127.0.0.1:'+port+'/v1'));
 server=await new Server().init();
 await server.request('thread/resume',{threadId:id,cwd:home,model:'gpt-6-astra',modelProvider:'custom',approvalPolicy:'never',sandbox:'read-only'});
 await server.turn(id,'Resume migrated legacy conversation');
 await server.close();server=null;
 await mkdir(path.join(home,'yilai-history-backups'),{recursive:true});
 const privateFragment='PRIVATE-HISTORY-MUST-NOT-APPEAR';
 await writeFile(path.join(home,'sessions','broken.jsonl'),JSON.stringify({type:'session_meta',payload:{id:'sentinel',model_provider:'custom',note:privateFragment}})+'\n');
 await writeFile(path.join(home,'yilai-history-backups','unrelated.json'),'invalid pending marker');
 const beforeHistory=await snapshots();
 operation('configure');
 assert.deepEqual(await snapshots(),beforeHistory,'API configuration/probe touched history/database files');
 assert.equal(await readFile(catalogPath,'utf8'),originalCatalog,'CCS catalog changed');
 assert.equal(await readFile(path.join(home,'config.toml'),'utf8'),configured);
 const retainedAuth='synthetic-auth-must-survive-reset';await writeFile(path.join(home,'auth.json'),retainedAuth);
 operation('cleanup');await assert.rejects(access(path.join(home,'config.toml')));
 assert.deepEqual(await snapshots(),beforeHistory);assert.equal(await readFile(path.join(home,'auth.json'),'utf8'),retainedAuth);
 const disabled=(await readdir(home)).filter(n=>n.startsWith('config.toml.disabled-'));assert.equal(disabled.length,1);assert.equal(await readFile(path.join(home,disabled[0]),'utf8'),configured);
 await writeFile(path.join(home,'config.toml'),'invalid=[TOML');
 const rejected=spawnSync(driver,['configure',home],{encoding:'utf8',windowsHide:true});assert.notEqual(rejected.status,0);assert.equal(await readFile(path.join(home,'auth.json'),'utf8'),retainedAuth);
 const logDir=path.join(home,'yilai-switcher-logs');const logs=(await Promise.all((await readdir(logDir)).map(n=>readFile(path.join(logDir,n),'utf8')))).join('\n');
 for(const secret of [privateFragment,'sk-isolated-test-only','sk-fake-file-credential'])assert(!logs.includes(secret));
 assert(!logs.includes('sync_history')&&!logs.includes('recover_history')&&!logs.includes('undo_history'));
 facts.passed=['API-only configuration and idempotence','managed three-model catalog installed; external catalog bytes retained','legacy yilai rollout/index unified; official runtime reads it; API resumes it','API bearer authentication without official login','native image tool and image header','real mock response via configured API','local configuration leaves existing/malformed history untouched','reset only disables configuration and preserves auth/history','invalid config fails without auth changes','logs contain no history operations or credentials'];
 facts.status='passed';await writeFile(path.join(root,'result.json'),JSON.stringify(facts,null,2));console.log(JSON.stringify(facts,null,2));
}catch(error){await writeFile(path.join(root,'failure.json'),JSON.stringify({error:String(error),stack:error.stack,stderr:server?.stderr},null,2));throw error;}
finally{if(server)await server.close();mock.closeAllConnections();await new Promise(r=>mock.close(r));}
