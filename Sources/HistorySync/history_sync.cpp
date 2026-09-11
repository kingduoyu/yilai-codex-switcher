#include "HistorySync.h"
#include "ConfigRewrite.h"
#include "vendor/json.hpp"
#include "vendor/sqlite3.h"
#include "../ConfigRewrite/vendor/toml.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {
void ensure(bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); }
std::string utf8(const fs::path &p) { auto s=p.u8string(); return {s.begin(),s.end()}; }
fs::path from(const std::string &s) { return fs::u8path(s); }
void noLinks(const fs::path &p) {
    auto current=fs::absolute(p).lexically_normal();
    while (!current.empty()) {
#ifdef _WIN32
        DWORD attrs=GetFileAttributesW(current.c_str());
        ensure(attrs==INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "History path contains a reparse point: " + utf8(current));
#else
        ensure(!fs::is_symlink(fs::symlink_status(current)), "History path contains a symbolic link: " + utf8(current));
#endif
        auto parent=current.parent_path(); if(parent==current) break; current=parent;
    }
}
std::string read(const fs::path &p) {
    noLinks(p);
    ensure(fs::is_regular_file(p) && fs::file_size(p)<=512ULL*1024*1024, "Invalid or oversized history file: "+utf8(p));
    std::ifstream f(p,std::ios::binary); ensure(bool(f),"Cannot read: "+utf8(p));
    std::ostringstream s; s<<f.rdbuf(); ensure(!f.bad(),"Read failed: "+utf8(p)); return s.str();
}
void write(const fs::path &p,const std::string &bytes) {
    noLinks(p); fs::create_directories(p.parent_path());
    auto tmp=p; tmp += ".yilai-tmp-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
#ifdef _WIN32
    HANDLE handle=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    ensure(handle!=INVALID_HANDLE_VALUE,"Cannot create temporary file"); DWORD written=0;
    bool ok=bytes.size()<=MAXDWORD && WriteFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if(!ok || !MoveFileExW(tmp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored; fs::remove(tmp,ignored); throw std::runtime_error("Atomic write failed: "+utf8(p));
    }
#else
    int fd=::open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600); ensure(fd>=0,"Cannot create temporary file");
    size_t done=0; bool ok=true;
    while(done<bytes.size()) { auto n=::write(fd,bytes.data()+done,bytes.size()-done); if(n<=0){ok=false;break;} done+=size_t(n); }
    ok=(::fsync(fd)==0)&&ok; ::close(fd);
    if(!ok || ::rename(tmp.c_str(),p.c_str())!=0) { std::error_code ignored; fs::remove(tmp,ignored); throw std::runtime_error("Atomic write failed: "+utf8(p)); }
#endif
}
struct Lock {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
#else
    int handle=-1;
#endif
    explicit Lock(const fs::path &home) {
        noLinks(home); fs::create_directories(home); auto p=home/".yilai-history.lock"; noLinks(p);
#ifdef _WIN32
        handle=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);
        ensure(handle!=INVALID_HANDLE_VALUE,"Another history operation is running");
#else
        handle=::open(p.c_str(),O_RDWR|O_CREAT,0600);
        if(handle<0 || flock(handle,LOCK_EX|LOCK_NB)!=0) { if(handle>=0)::close(handle); throw std::runtime_error("Another history operation is running"); }
#endif
    }
    ~Lock() {
#ifdef _WIN32
        if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
#else
        if(handle>=0){flock(handle,LOCK_UN);::close(handle);}
#endif
    }
};
struct Db {
    sqlite3 *p=nullptr; bool transaction=false;
    Db(const fs::path &path,bool create=false) {
        noLinks(path);
        int rc=sqlite3_open_v2(utf8(path).c_str(),&p,SQLITE_OPEN_READWRITE|(create?SQLITE_OPEN_CREATE:0),nullptr);
        if(rc!=SQLITE_OK) { if(p)sqlite3_close(p); p=nullptr; throw std::runtime_error("Cannot open history database: "+utf8(path)); }
        sqlite3_busy_timeout(p,1500);
    }
    ~Db(){ if(p){if(transaction)sqlite3_exec(p,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(p);} }
    void exec(const char *sql) { ensure(sqlite3_exec(p,sql,nullptr,nullptr,nullptr)==SQLITE_OK,"SQLite operation failed (close Codex and CCS, then retry)"); }
    void begin(){exec("BEGIN IMMEDIATE");transaction=true;}
    void commit(){exec("COMMIT");transaction=false;}
    void rollback(){if(transaction){exec("ROLLBACK");transaction=false;}}
};
struct Stmt {
    sqlite3_stmt *p=nullptr;
    Stmt(Db &db,const char *sql){ensure(sqlite3_prepare_v2(db.p,sql,-1,&p,nullptr)==SQLITE_OK,"Unsupported history database schema");}
    ~Stmt(){if(p)sqlite3_finalize(p);}
    void bind(int i,const Json &value){int rc=value.is_null()?sqlite3_bind_null(p,i):sqlite3_bind_text(p,i,value.get_ref<const std::string&>().c_str(),-1,SQLITE_TRANSIENT);ensure(rc==SQLITE_OK,"SQLite bind failed");}
};
Json rows(Db &db) {
    Stmt integrity(db,"PRAGMA quick_check");ensure(sqlite3_step(integrity.p)==SQLITE_ROW && std::string(reinterpret_cast<const char*>(sqlite3_column_text(integrity.p,0)))=="ok","History database integrity check failed");
    Stmt q(db,"SELECT id,model_provider FROM threads ORDER BY id"); Json out=Json::array();int rc;
    while((rc=sqlite3_step(q.p))==SQLITE_ROW){
        ensure(sqlite3_column_type(q.p,0)==SQLITE_TEXT,"Invalid thread ID");
        ensure(sqlite3_column_type(q.p,1)==SQLITE_TEXT || sqlite3_column_type(q.p,1)==SQLITE_NULL,"Invalid provider field");
        auto id=std::string(reinterpret_cast<const char*>(sqlite3_column_text(q.p,0)));
        Json provider=sqlite3_column_type(q.p,1)==SQLITE_NULL?Json(nullptr):Json(reinterpret_cast<const char*>(sqlite3_column_text(q.p,1)));
        out.push_back({id,provider});
    }ensure(rc==SQLITE_DONE,"History database read failed");return out;
}
void backupDb(Db &db,const fs::path &target) {
    Db dest(target,true); sqlite3_backup *b=sqlite3_backup_init(dest.p,"main",db.p,"main");ensure(b!=nullptr,"Database backup failed");
    int rc=sqlite3_backup_step(b,-1);int finish=sqlite3_backup_finish(b);
    ensure(rc==SQLITE_DONE && finish==SQLITE_OK && rows(db)==rows(dest),"Database backup verification failed");
}
void updateRows(Db &db,const Json &changes,bool reverse=false) {
    for(const auto &row:changes){
        Stmt q(db,"UPDATE threads SET model_provider=? WHERE id=? AND model_provider IS ?");
        q.bind(1,row[reverse?1:2]);q.bind(2,row[0]);q.bind(3,row[reverse?2:1]);
        ensure(sqlite3_step(q.p)==SQLITE_DONE && sqlite3_changes(db.p)==1,"History changed during synchronization; no concurrent writers are allowed");
    }
}
struct Meta { size_t offset=0,length=0;std::string raw,id;Json record;bool bom=false,cr=false; };
Meta metadata(const std::string &bytes) {
    Meta result;bool found=false;size_t start=0;
    while(start<bytes.size()) {
        auto end=bytes.find('\n',start);if(end==std::string::npos)end=bytes.size();
        auto line=bytes.substr(start,end-start);bool bom=start==0 && line.rfind("\xEF\xBB\xBF",0)==0;
        std::string parse=bom?line.substr(3):line;bool cr=!parse.empty()&&parse.back()=='\r';if(cr)parse.pop_back();
        if(!parse.empty()) {
            auto value=Json::parse(parse);ensure(value.is_object(),"Invalid JSONL record");
            if(value.value("type",std::string())=="session_meta") {
                ensure(!found && value.contains("payload") && value["payload"].is_object(),"Missing or duplicate session metadata");found=true;
                auto &payload=value["payload"];ensure(payload.contains("id")&&payload["id"].is_string(),"Invalid session ID");
                ensure(!payload.contains("model_provider")||payload["model_provider"].is_string(),"Invalid session provider");
                result={start,end-start,line,payload["id"].get<std::string>(),value,bom,cr};
            }
        }
        start=end+1;
    }
    ensure(found,"Session metadata not found");return result;
}
std::string changedLine(const Meta &m,const Json &provider) {
    auto value=m.record;if(provider.is_null())value["payload"].erase("model_provider");else value["payload"]["model_provider"]=provider;
    return (m.bom?"\xEF\xBB\xBF":"")+value.dump()+(m.cr?"\r":"");
}
Json currentProvider(const Meta &m){return m.record["payload"].contains("model_provider")?m.record["payload"]["model_provider"]:Json(nullptr);}
std::vector<fs::path> sessionFiles(const fs::path &home) {
    std::vector<fs::path> out;
    for(const char *name:{"sessions","archived_sessions"}) {
        auto dir=home/name;noLinks(dir);if(!fs::exists(dir))continue;
        for(auto it=fs::recursive_directory_iterator(dir);it!=fs::recursive_directory_iterator();++it){
            noLinks(it->path());ensure(it.depth()<16,"History directory is too deep");
            if(it->is_regular_file()&&it->path().extension()==".jsonl")out.push_back(it->path());
        }
    }std::sort(out.begin(),out.end());return out;
}
std::vector<fs::path> databases(const fs::path &home,const std::string &config) {
    auto doc=toml::parse(config);std::vector<fs::path> dirs{home};
    std::string extra=doc["sqlite_home"].value_or(std::string());
    if(extra.empty()){const char *env=std::getenv("CODEX_SQLITE_HOME");if(env)extra=env;}
    if(!extra.empty()){
        if(extra=="~"||extra.rfind("~/",0)==0||extra.rfind("~\\",0)==0){
#ifdef _WIN32
            wchar_t *value=nullptr;size_t n=0;_wdupenv_s(&value,&n,L"USERPROFILE");ensure(value!=nullptr,"Cannot expand sqlite_home");fs::path user(value);free(value);
#else
            const char *value=std::getenv("HOME");ensure(value!=nullptr,"Cannot expand sqlite_home");fs::path user(value);
#endif
            extra=utf8(extra=="~"?user:user/from(extra.substr(2)));
        }
        auto p=from(extra);ensure(p.is_absolute(),"sqlite_home must be an absolute path");p=fs::weakly_canonical(p).lexically_normal();if(p!=home)dirs.push_back(p);
    }
    std::set<fs::path> out;
    for(const auto &dir:dirs){noLinks(dir);if(!fs::exists(dir))continue;for(const auto &entry:fs::directory_iterator(dir)) {
        auto name=utf8(entry.path().filename());if(name.rfind("state_",0)!=0||entry.path().extension()!=".sqlite")continue;
        ensure(name=="state_5.sqlite","Unsupported state database version; history left unchanged");noLinks(entry.path());out.insert(entry.path());
    }}return {out.begin(),out.end()};
}
std::string normalize(const std::string &config) {
    char *error=nullptr;char *result=yilai_apply_config(config.c_str(),"",YILAI_UNIFY_HISTORY,&error);
    std::string diagnostic=error?error:"Cannot unify connection";if(error)yilai_config_free(error);
    ensure(result!=nullptr,diagnostic);std::string out(result);yilai_config_free(result);return out;
}
Json operate(const fs::path &input,bool restore,int failAfter=0) {
    auto home=fs::weakly_canonical(fs::absolute(input)).lexically_normal();Lock lock(home);
    auto configPath=home/"config.toml";std::string oldConfig=fs::exists(configPath)?read(configPath):"";
    std::string newConfig=restore?oldConfig:normalize(oldConfig);
    auto parent=home/"yilai-history-backups";noLinks(parent);Json source;
    auto pointer=parent/"latest.json";
    if(fs::exists(parent/"pending.json")) {
        auto pending=Json::parse(read(parent/"pending.json"));auto pendingName=pending.at("generation").get<std::string>();
        ensure(!pendingName.empty()&&pendingName.find_first_not_of("0123456789-")==std::string::npos,"Invalid pending backup pointer");
        auto pendingPlan=Json::parse(read(parent/pendingName/"manifest.json"));
        auto status=pendingPlan.value("status",std::string());
        if(status=="rolled_back"||status=="restored")fs::remove(parent/"pending.json");
        else { ensure(restore,"An interrupted synchronization needs recovery. Choose Undo last sync first."); pointer=parent/"pending.json"; }
    }
    if(restore && fs::exists(pointer)) {
        auto latest=Json::parse(read(pointer));auto generation=latest.at("generation").get<std::string>();
        ensure(!generation.empty()&&generation.find_first_not_of("0123456789-")==std::string::npos,"Invalid history backup pointer");
        source=Json::parse(read(parent/generation/"manifest.json"));
        ensure(source.at("home")==utf8(home),"History backup belongs to another directory");
        ensure(source.value("kind",std::string())=="sync" && source.value("status",std::string())!="restored","No synchronization to undo");
        if(oldConfig==source.at("new_config"))newConfig=source.at("old_config");
    } else if(restore)throw std::runtime_error("No local history synchronization backup found");
    std::string generation=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    auto backup=parent/generation;ensure(!fs::exists(backup),"Backup generation collision");
    Json plan={{"version",1},{"kind",restore?"restore":"sync"},{"home",utf8(home)},{"old_config",oldConfig},{"new_config",newConfig},{"status","prepared"},{"files",Json::array()},{"databases",Json::array()}};
    std::set<std::string> seenIds;
    auto files=restore?std::vector<fs::path>{}:sessionFiles(home);
    if(restore)for(const auto &record:source["files"])files.push_back(from(record.at("path")));
    for(const auto &file:files) {
        auto relative=file.lexically_relative(home);ensure(!relative.empty()&&*relative.begin()!="..","Invalid history backup path");
        ensure(fs::exists(file),"A synchronized history file is missing; no changes were made");
        auto bytes=read(file);auto meta=metadata(bytes);Json fromProvider=currentProvider(meta),toProvider="custom";
        if(!restore){ensure(seenIds.insert(meta.id).second,"Duplicate session IDs require manual review");}
        if(restore){auto found=std::find_if(source["files"].begin(),source["files"].end(),[&](const Json &r){return r.at("path")==utf8(file);});ensure(found!=source["files"].end()&&found->at("id")==meta.id,"Session identity changed");if(fromProvider!="custom")continue;toProvider=found->at("from");}
        if(fromProvider==toProvider)continue;
        plan["files"].push_back({{"path",utf8(file)},{"id",meta.id},{"from",fromProvider},{"to",toProvider},{"old_line",meta.raw},{"new_line",changedLine(meta,toProvider)}, {"offset",meta.offset}, {"size",bytes.size()}, {"backup",std::to_string(plan["files"].size())+".jsonl"}});
    }
    auto dbPaths=restore?std::vector<fs::path>{}:databases(home,oldConfig);
    if(restore)for(const auto &record:source["databases"])dbPaths.push_back(from(record.at("path")));
    std::vector<std::unique_ptr<Db>> connections;
    for(const auto &p:dbPaths){
        auto db=std::make_unique<Db>(p);auto snapshot=rows(*db);Json changes=Json::array();
        for(const auto &row:snapshot){
            if(row[1]=="custom" && !restore)continue;
            Json target="custom";
            if(restore){
                if(row[1]!="custom")continue;
                auto dbPlan=std::find_if(source["databases"].begin(),source["databases"].end(),[&](const Json &x){return x.at("path")==utf8(p);});ensure(dbPlan!=source["databases"].end(),"Invalid database backup entry");
                auto prior=std::find_if((*dbPlan)["rows"].begin(),(*dbPlan)["rows"].end(),[&](const Json &x){return x[0]==row[0];});if(prior==(*dbPlan)["rows"].end())continue;target=(*prior)[1];
            }
            changes.push_back({row[0],row[1],target});
        }
        if(changes.empty())continue;
        plan["databases"].push_back({{"path",utf8(p)},{"rows",changes},{"backup",std::to_string(connections.size())+".sqlite"}});connections.push_back(std::move(db));
    }
    if(plan["files"].empty()&&connections.empty()&&newConfig==oldConfig)return {{"files",0},{"rows",0},{"backup",""},{"restored",restore}};
    fs::create_directories(backup);
#ifndef _WIN32
    ::chmod(parent.c_str(),0700);::chmod(backup.c_str(),0700);
#endif
    write(backup/"config.toml",oldConfig);
    for(const auto &edit:plan["files"]){auto bytes=read(from(edit.at("path")));auto meta=metadata(bytes);ensure(meta.raw==edit["old_line"]&&meta.offset==edit["offset"]&&bytes.size()==edit["size"],"History changed during backup");write(backup/edit.at("backup").get<std::string>(),bytes);}
    for(size_t i=0;i<connections.size();++i)backupDb(*connections[i],backup/plan["databases"][i]["backup"].get<std::string>());
    write(backup/"manifest.json",plan.dump(2));
    if(!restore)write(parent/"pending.json",Json({{"generation",generation}}).dump());
    std::vector<size_t> changedFiles,committed;bool configChanged=false;size_t count=0;
    try {
        for(auto &db:connections)db->begin();
        for(size_t i=0;i<connections.size();++i)updateRows(*connections[i],plan["databases"][i]["rows"]);
        for(size_t i=0;i<plan["files"].size();++i){const auto &edit=plan["files"][i];auto p=from(edit.at("path"));auto bytes=read(p);auto original=read(backup/edit.at("backup").get<std::string>());ensure(bytes==original,"History changed after backup");bytes.replace(edit["offset"].get<size_t>(),edit["old_line"].get<std::string>().size(),edit["new_line"].get<std::string>());write(p,bytes);changedFiles.push_back(i);if(failAfter>0&&int(changedFiles.size())==failAfter)throw std::runtime_error("Synthetic rollback test");}
        ensure((fs::exists(configPath)?read(configPath):"")==oldConfig,"Configuration changed during synchronization");
        if(newConfig!=oldConfig){write(configPath,newConfig);configChanged=true;}
        for(size_t i=0;i<connections.size();++i){connections[i]->commit();committed.push_back(i);count+=plan["databases"][i]["rows"].size();}
        plan["status"]="complete";write(backup/"manifest.json",plan.dump(2));
        if(!restore)write(parent/"latest.json",Json({{"generation",generation}}).dump());
        else {source["status"]="restored";auto latest=Json::parse(read(pointer));write(parent/latest.at("generation").get<std::string>()/"manifest.json",source.dump(2));}
        if(fs::exists(parent/"pending.json"))fs::remove(parent/"pending.json");
    } catch (...) {
        bool rollbackOk=true;
        for(auto &db:connections)try{db->rollback();}catch(...){rollbackOk=false;}
        for(auto i:committed)try{connections[i]->begin();updateRows(*connections[i],plan["databases"][i]["rows"],true);connections[i]->commit();}catch(...){rollbackOk=false;}
        if(configChanged)try{ensure(read(configPath)==newConfig,"Config changed during rollback");write(configPath,oldConfig);}catch(...){rollbackOk=false;}
        for(auto it=changedFiles.rbegin();it!=changedFiles.rend();++it)try{auto &edit=plan["files"][*it];auto p=from(edit.at("path"));auto bytes=read(p);auto meta=metadata(bytes);ensure(meta.raw==edit["new_line"],"Metadata changed during rollback");bytes.replace(meta.offset,meta.length,edit["old_line"].get<std::string>());write(p,bytes);}catch(...){rollbackOk=false;}
        plan["status"]=rollbackOk?"rolled_back":"recovery_required";try{write(backup/"manifest.json",plan.dump(2));}catch(...){}
        if(!rollbackOk)throw std::runtime_error("Rollback incomplete. Keep backups and inspect: "+utf8(backup));
        throw;
    }
    return {{"files",plan["files"].size()},{"rows",count},{"backup",utf8(backup)},{"restored",restore}};
}
char *copy(const std::string &s){auto *p=static_cast<char*>(std::malloc(s.size()+1));if(p)std::memcpy(p,s.c_str(),s.size()+1);return p;}
void selfTest(){
    auto temp=fs::weakly_canonical(fs::temp_directory_path()).lexically_normal();auto home=temp/("YilaiHistory-test-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));ensure(home.parent_path()==temp,"Unsafe test path");
    fs::create_directories(home);struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}}cleanup{home};
    const std::string config="model='gpt-6-astra'\n";write(home/"config.toml",config);write(home/"auth.json","synthetic-auth");write(home/"thread_history_1.sqlite","unrelated sentinel");
    for(int i=0;i<4;++i){auto file=home/(i==3?"archived_sessions":"sessions")/(std::to_string(i)+".jsonl");std::string provider=i==0?"openai":i==1?"yilai":i==2?"other":"custom";Json meta={{"type","session_meta"},{"payload",{{"id",std::to_string(i)},{"model_provider",provider}}}};write(file,meta.dump()+"\r\n{\"type\":\"event_msg\",\"payload\":{\"message\":\"unchanged text\"}}\n");}
    {Db db(home/"state_5.sqlite",true);db.exec("CREATE TABLE threads(id TEXT PRIMARY KEY,model_provider TEXT,title TEXT,archived INTEGER)");db.exec("INSERT INTO threads VALUES('0','openai','keep',0),('1','yilai','keep2',0),('2','other','keep3',0),('3','custom','archived',1)");}
    const auto before=read(home/"sessions/0.jsonl");bool failed=false;try{operate(home,false,1);}catch(...){failed=true;}
    ensure(failed&&read(home/"sessions/0.jsonl")==before&&read(home/"config.toml")==config,"Injected failure did not roll back files/config");
    {Db db(home/"state_5.sqlite");ensure(rows(db)[0][1]=="openai","Injected failure committed database");}
    auto synced=operate(home,false);ensure(synced["files"]==3&&synced["rows"]==3,"Wrong migration count");
    ensure(read(home/"auth.json")=="synthetic-auth"&&read(home/"thread_history_1.sqlite")=="unrelated sentinel","Unrelated files changed");
    ensure(operate(home,false)["files"]==0,"Repeated sync not idempotent");
    auto message="{\"type\":\"event_msg\",\"payload\":{\"message\":\"new message after sync\"}}\n";write(home/"sessions/0.jsonl",read(home/"sessions/0.jsonl")+message);
    {Db db(home/"state_5.sqlite");db.exec("UPDATE threads SET title='new title' WHERE id='0'");db.exec("INSERT INTO threads VALUES('new','custom','new thread',0)");}
    operate(home,true);ensure(read(home/"sessions/0.jsonl").find(message)!=std::string::npos,"Restore lost new messages");
    {Db db(home/"state_5.sqlite");auto data=rows(db);ensure(data[0][1]=="openai"&&data.back()[1]=="custom","Restore changed newer thread or lost source provider");Stmt q(db,"SELECT title FROM threads WHERE id='0'");ensure(sqlite3_step(q.p)==SQLITE_ROW&&std::string(reinterpret_cast<const char*>(sqlite3_column_text(q.p,0)))=="new title","Restore lost new title");}
    ensure(read(home/"config.toml")==config,"Restore did not restore unchanged config");
    write(home/"sessions/broken.jsonl","not json");failed=false;try{operate(home,false);}catch(...){failed=true;}ensure(failed&&read(home/"config.toml")==config,"Malformed history modified configuration");
}
}
extern "C" char *yilai_sync_history(const char *home,int restore,char **error){if(error)*error=nullptr;try{ensure(home!=nullptr,"Missing Codex home");auto *result=copy(operate(from(home),restore!=0).dump());ensure(result!=nullptr,"Out of memory");return result;}catch(const std::exception &e){if(error)*error=copy(e.what());}catch(...){if(error)*error=copy("History operation failed");}return nullptr;}
extern "C" int yilai_history_self_test(char **error){if(error)*error=nullptr;try{selfTest();return 1;}catch(const std::exception &e){if(error)*error=copy(e.what());return 0;}catch(...){if(error)*error=copy("History self-test failed");return 0;}}
