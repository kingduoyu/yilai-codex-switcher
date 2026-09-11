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
const catalog={models:[{...JSON.parse(await readFile(templatePath,'utf8')),slug:'gpt-6-astra',display_name:'gpt-6-astra'}]};
const catalogPath=path.join(home,'cc-switch-model-catalog.json');
await writeFile(catalogPath,JSON.stringify(catalog,null,2));
const config="model='gpt-6-astra'\nmodel_provider='custom'\nmodel_catalog_json="+JSON.stringify(catalogPath.replaceAll('\\','/'))+"\ncli_auth_credentials_store='file'\napproval_policy='never'\nsandbox_mode='read-only'\n[analytics]\nenabled=false\n[model_providers.custom]\nname='CCS synthetic'\nbase_url='http://127.0.0.1:"+port+"/v1'\nwire_api='responses'\nrequires_openai_auth=false\nexperimental_bearer_token='sk-synthetic-local-only'\n[model_providers.custom.http_headers]\nX-Keep='unchanged'\n";
const auth=JSON.stringify({OPENAI_API_KEY:'sk-fake-file-credential'});
await writeFile(path.join(home,'config.toml'),config);await writeFile(path.join(home,'auth.json'),auth);
function operation(action){const r=spawnSync(driver,[action,home],{encoding:'utf8',windowsHide:true,env:{...process.env,CODEX_HOME:home,CODEX_SQLITE_HOME:home}});assert.equal(r.status,0,r.stderr);}
class Server {
 constructor(){this.pending=new Map();this.notifications=[];this.id=0;this.stderr='';this.buffer='';this.child=spawn(codex,['app-server'],{cwd:home,env:{...process.env,CODEX_HOME:home,CODEX_SQLITE_HOME:home},windowsHide:true,stdio:['pipe','pipe','pipe']});this.done=new Promise(resolve=>this.child.on('close',resolve));this.child.stderr.on('data',x=>this.stderr+=x);this.child.stdout.on('data',x=>{this.buffer+=x;let i;while((i=this.buffer.indexOf('\n'))>=0){const line=this.buffer.slice(0,i);this.buffer=this.buffer.slice(i+1);let msg;try{msg=JSON.parse(line);}catch{continue;}if(this.pending.has(msg.id)){const w=this.pending.get(msg.id);this.pending.delete(msg.id);clearTimeout(w.timer);msg.error?w.reject(new Error(JSON.stringify(msg.error))):w.resolve(msg.result);}else{this.notifications.push(msg);}}});this.child.on('exit',code=>{for(const w of this.pending.values()){clearTimeout(w.timer);w.reject(new Error('Runtime exit '+code+' '+this.stderr));}this.pending.clear();});}
 request(method,params){return new Promise((resolve,reject)=>{const id=++this.id;const timer=setTimeout(()=>{this.pending.delete(id);reject(new Error('Timeout '+method+' '+this.stderr));},25000);this.pending.set(id,{resolve,reject,timer});this.child.stdin.write(JSON.stringify({id,method,params})+'\n');});}
 async init(){await this.request('initialize',{clientInfo:{name:'rewrite_integration',version:'3.3.0'},capabilities:{experimentalApi:true}});this.child.stdin.write(JSON.stringify({method:'initialized',params:{}})+'\n');return this;}
 async turn(id,text){const result=await this.request('turn/start',{threadId:id,input:[{type:'text',text,text_elements:[]}]});const deadline=Date.now()+25000;while(Date.now()<deadline){const n=this.notifications.find(n=>n.method==='turn/completed'&&n.params.threadId===id&&n.params.turn.id===result.turn.id);if(n){assert.equal(n.params.turn.status,'completed',JSON.stringify(n));return n;}await new Promise(r=>setTimeout(r,30));}throw new Error('Turn timeout '+this.stderr+' '+JSON.stringify(this.notifications));}
 async close(){this.child.stdin.end();this.child.kill();await this.done;}
}
const facts={root,runtime:spawnSync(codex,['--version'],{encoding:'utf8',windowsHide:true}).stdout.trim()};
let server;
async function filesBelow(dir){let out=[];for(const d of await readdir(dir,{withFileTypes:true})){const p=path.join(dir,d.name);if(d.isDirectory())out.push(...await filesBelow(p));else if(d.name.endsWith('.jsonl'))out.push(p);}return out;}
try{
 operation('images'); const enhanced=await readFile(path.join(home,'config.toml'),'utf8');operation('images');assert.equal(await readFile(path.join(home,'config.toml'),'utf8'),enhanced);assert.equal(await readFile(path.join(home,'auth.json'),'utf8'),auth);assert.deepEqual(JSON.parse(await readFile(catalogPath,'utf8')),catalog);
 server=await new Server().init();
 const models=await server.request('model/list',{includeHidden:false,limit:100});assert(models.data.some(m=>(m.model??m.id)==='gpt-6-astra'));facts.models=models.data.map(m=>m.model??m.id);
 const effective=(await server.request('config/read',{includeLayers:false})).config;assert.equal(effective.model,'gpt-6-astra');assert.equal(effective.model_catalog_json.replaceAll('\\','/'),catalogPath.replaceAll('\\','/'));assert.equal(effective.features.image_generation,true);assert.equal(effective.model_providers.custom.http_headers['X-Keep'],'unchanged');
 const t=await server.request('thread/start',{cwd:home,model:'gpt-6-astra',modelProvider:'custom',approvalPolicy:'never',sandbox:'read-only'});const id=t.thread.id;facts.threadId=id;await server.turn(id,'Synthetic message before history sync');
 const originalRead=await server.request('thread/read',{threadId:id,includeTurns:true});assert(JSON.stringify(originalRead).includes('Synthetic reply'));
 await server.close();server=null;
 const rollouts=await filesBelow(path.join(home,'sessions'));assert.equal(rollouts.length,1);const rollout=rollouts[0];
 let lines=(await readFile(rollout,'utf8')).split('\n');const idx=lines.findIndex(l=>l&&JSON.parse(l).type==='session_meta');const meta=JSON.parse(lines[idx]);meta.payload.model_provider='yilai';lines[idx]=JSON.stringify(meta);await writeFile(rollout,lines.join('\n'));
 const before=await readFile(rollout,'utf8');
 // A separate interpreter updates only the provider in the real runtime schema.
 const py=spawnSync('python',['-c',"import sqlite3,sys; c=sqlite3.connect(sys.argv[1]); c.execute('update threads set model_provider=? where id=?',('yilai',sys.argv[2])); c.commit()",path.join(home,'state_5.sqlite'),id],{encoding:'utf8',windowsHide:true});assert.equal(py.status,0,py.stderr);
 operation('configure');await assert.rejects(access(path.join(home,'auth.json')));
 const configured=await readFile(path.join(home,'config.toml'),'utf8');
 assert(configured.includes('requires_openai_auth = false'));assert(configured.includes('sk-isolated-test-only'));
 await writeFile(path.join(home,'config.toml'),configured.replace('https://api.yilai-ai.com','http://127.0.0.1:'+port+'/v1'));
 const after=await readFile(rollout,'utf8');assert.equal(JSON.parse(after.split('\n')[idx]).payload.model_provider,'custom');assert.deepEqual(after.split('\n').filter((_,i)=>i!==idx),before.split('\n').filter((_,i)=>i!==idx));
 server=await new Server().init();
 const login=await server.request('account/read',{refreshToken:false});assert.equal(login.requiresOpenaiAuth,false);assert.equal(login.account,null);facts.apiWithoutOfficialLogin=login;
 const listing=await server.request('thread/list',{modelProviders:['custom'],limit:100});assert(listing.data.some(t=>t.id===id));
 const read=await server.request('thread/read',{threadId:id,includeTurns:true});assert(JSON.stringify(read).includes('Synthetic message before history sync'));assert(JSON.stringify(read).includes('Synthetic reply'));
 await server.request('thread/resume',{threadId:id});await server.turn(id,'Synthetic message after history sync');
 const resumed=await server.request('thread/read',{threadId:id,includeTurns:true});assert(JSON.stringify(resumed).includes('Synthetic message after history sync'));
 await server.close();server=null;
 const beforeUndo=await readFile(rollout,'utf8');operation('undo');const undone=await readFile(rollout,'utf8');assert.equal(JSON.parse(undone.split('\n')[idx]).payload.model_provider,'yilai');assert.deepEqual(undone.split('\n').filter((_,i)=>i!==idx),beforeUndo.split('\n').filter((_,i)=>i!==idx));await assert.rejects(access(path.join(home,'auth.json')));
 assert(requests.filter(x=>x.url.endsWith('/responses')).length>=2);for(const req of requests.filter(x=>x.url.endsWith('/responses'))){assert.equal(req.headers['x-openai-actor-authorization'],'local-image-extension');if(req===requests.filter(x=>x.url.endsWith('/responses'))[0]) assert.equal(req.headers['x-keep'],'unchanged'); else assert.equal(req.headers['x-keep'],undefined, 'Switching providers must remove the previous provider header');assert.equal(req.body.model,'gpt-6-astra');assert(req.body.tools.some(t=>t.type==='namespace'&&t.name==='image_gen'&&t.tools.some(f=>f.name==='imagegen')), 'Native image tool missing');}
 // Simulate CCS selecting official mode, then another local login credential.
 await writeFile(path.join(home,'config.toml'),"model='gpt-6-astra'\nmodel_catalog_json="+JSON.stringify(catalogPath.replaceAll('\\','/'))+"\ncli_auth_credentials_store='file'\n[analytics]\nenabled=false\n");
 operation('official');await assert.rejects(access(path.join(home,'auth.json')));
 const officialConfig=await readFile(path.join(home,'config.toml'),'utf8');assert(officialConfig.includes('requires_openai_auth = true'));assert(!officialConfig.includes('base_url'));assert(!officialConfig.includes('experimental_bearer_token'));
 for(const account of ['first','second']){
  const credential=JSON.stringify({OPENAI_API_KEY:'sk-synthetic-'+account});await writeFile(path.join(home,'auth.json'),credential);
  server=await new Server().init();const visible=await server.request('thread/list',{modelProviders:['custom'],limit:100});assert(visible.data.some(t=>t.id===id));const detail=await server.request('thread/read',{threadId:id,includeTurns:true});assert(JSON.stringify(detail).includes('Synthetic message after history sync'));await server.close();server=null;
  assert.equal(await readFile(path.join(home,'auth.json'),'utf8'),credential);
 }
 assert.equal(requests.filter(x=>x.url.endsWith('/responses')).at(-1).headers.authorization,'Bearer sk-isolated-test-only');
 facts.passed=['one-click API config deletes auth and automatically syncs legacy history','account/read confirms no official login required after auth deletion','one-click official switch automatically syncs history','official alias preserves visible local history across credential changes','native image_gen.imagegen advertised','internal image-only operation preserves model/catalog/auth','enhance idempotent','native model/list gpt-6-astra','real thread written by app-server','legacy provider sync preserves content','native list/read/resume after sync','new message retained by undo','custom image header reaches local mock'];facts.requestTools=requests.filter(x=>x.url.endsWith('/responses')).map(x=>(x.body.tools??[]).map(t=>t.name??t.type));facts.status='passed';
 await writeFile(path.join(root,'result.json'),JSON.stringify(facts,null,2));console.log(JSON.stringify(facts,null,2));
}catch(error){await writeFile(path.join(root,'failure.json'),JSON.stringify({error:String(error),stack:error.stack,stderr:server?.stderr,notifications:server?.notifications,requests},null,2));throw error;}
finally{if(server)await server.close();mock.closeAllConnections();await new Promise(r=>mock.close(r));}
