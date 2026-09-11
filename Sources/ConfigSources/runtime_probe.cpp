#include "runtime_probe.h"
#include "../Shared/vendor/json.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace yilai_sources {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr size_t outputLimit = 8 * 1024 * 1024;
using Clock = std::chrono::steady_clock;
[[noreturn]] void fail(const char *category) {
  throw std::runtime_error(std::string("Codex config probe: ") + category);
}
bool regular(const fs::path &path) {
  std::error_code error;
  return fs::is_regular_file(path, error);
}
std::string utf8(const fs::path &path) { return path.u8string(); }
// config/read starts an app server; isolate its databases from local history.
struct ProbeStorage {
  fs::path parent, path;
  ProbeStorage() {
    static std::atomic<unsigned long long> sequence{0};
    parent=fs::weakly_canonical(fs::temp_directory_path());
    for(int attempt=0;attempt<20;++attempt) {
      path=parent/("yilai-config-probe-"+std::to_string(Clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++));
      if(fs::create_directory(path)) {
#ifndef _WIN32
        fs::permissions(path,fs::perms::owner_all,fs::perm_options::replace);
#endif
        return;
      }
    }
    fail("temporary probe storage unavailable");
  }
  ~ProbeStorage() {
    std::error_code error;
    if(path.parent_path()==parent && path.filename().string().rfind("yilai-config-probe-",0)==0 && !fs::is_symlink(fs::symlink_status(path,error)))
      fs::remove_all(path,error);
  }
};
#ifdef _WIN32
std::wstring environment(const wchar_t *name) {
  auto count = GetEnvironmentVariableW(name, nullptr, 0);
  if (!count) return {};
  std::wstring value(count, L'\0');
  auto length = GetEnvironmentVariableW(name, value.data(), count);
  if (!length || length >= count) return {};
  value.resize(length);
  return value;
}
std::wstring quote(const std::wstring &value) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t ch : value) {
    if (ch == L'\\') { ++slashes; continue; }
    if (ch == L'\"') result.append(slashes * 2 + 1, L'\\');
    else result.append(slashes, L'\\');
    slashes = 0;
    result += ch;
  }
  result.append(slashes * 2, L'\\');
  return result + L'\"';
}
struct Child {
  HANDLE input = nullptr, output = nullptr, errors = nullptr;
  HANDLE process = nullptr, job = nullptr;
  ~Child() {
    // Closing the job terminates descendants as well as the app server.
    if (job) CloseHandle(job);
    if (process) { TerminateProcess(process, 1); WaitForSingleObject(process, 2000); CloseHandle(process); }
    for (auto handle : {input, output, errors}) if (handle) CloseHandle(handle);
  }
  void launch(const fs::path &runtime, const fs::path &home, const fs::path &sqliteHome) {
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE childInput = nullptr, childOutput = nullptr, childErrors = nullptr;
    struct Ends {
      HANDLE &a, &b, &c;
      ~Ends() { for (auto h : {a,b,c}) if (h) CloseHandle(h); }
    } ends{childInput, childOutput, childErrors};
    if (!CreatePipe(&childInput, &input, &attributes, 65536) ||
        !CreatePipe(&output, &childOutput, &attributes, 65536) ||
        !CreatePipe(&errors, &childErrors, &attributes, 65536)) fail("pipe creation failed");
    for (auto handle : {input,output,errors})
      if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0)) fail("pipe setup failed");
    // Restrict inheritance to these three handles in this multithreaded GUI.
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) fail("process setup failed");
    struct AttributeCleanup { LPPROC_THREAD_ATTRIBUTE_LIST list; ~AttributeCleanup(){DeleteProcThreadAttributeList(list);} } cleanup{list};
    HANDLE inherited[]{childInput,childOutput,childErrors};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr)) fail("process setup failed");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = childInput;
    startup.StartupInfo.hStdOutput = childOutput;
    startup.StartupInfo.hStdError = childErrors;
    startup.lpAttributeList = list;
    std::vector<std::wstring> variables;
    auto block = GetEnvironmentStringsW();
    if (!block) fail("environment unavailable");
    for (const wchar_t *p = block; *p; p += wcslen(p) + 1) {
      std::wstring item(p);
      if (_wcsnicmp(item.c_str(), L"CODEX_HOME=", 11) != 0 &&
          _wcsnicmp(item.c_str(), L"CODEX_SQLITE_HOME=", 18) != 0) variables.push_back(item);
    }
    FreeEnvironmentStringsW(block);
    variables.push_back(L"CODEX_HOME=" + home.wstring());
    variables.push_back(L"CODEX_SQLITE_HOME=" + sqliteHome.wstring());
    std::sort(variables.begin(), variables.end(), [](const auto &a, const auto &b){return _wcsicmp(a.c_str(),b.c_str()) < 0;});
    std::vector<wchar_t> environmentBlock;
    for (const auto &item : variables) { environmentBlock.insert(environmentBlock.end(), item.begin(), item.end()); environmentBlock.push_back(0); }
    environmentBlock.push_back(0);
    job = CreateJobObjectW(nullptr, nullptr);
    if (!job) fail("process containment failed");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) fail("process containment failed");
    PROCESS_INFORMATION info{};
    auto command = quote(runtime.wstring()) + L" app-server";
    if (!CreateProcessW(runtime.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
        environmentBlock.data(), nullptr, &startup.StartupInfo, &info)) fail("launch failed");
    process = info.hProcess;
    if (!AssignProcessToJobObject(job, process)) { CloseHandle(info.hThread); fail("process containment failed"); }
    auto resumed = ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    if (resumed == DWORD(-1)) fail("launch failed");
  }
  void send(const std::string &data) {
    if (data.size() > 16384) fail("request too large");
    DWORD written = 0;
    if (!WriteFile(input, data.data(), DWORD(data.size()), &written, nullptr) || written != data.size()) fail("input pipe closed");
  }
  size_t read(HANDLE handle, char *buffer, size_t capacity) {
    DWORD available = 0, received = 0;
    if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
      if (GetLastError() == ERROR_BROKEN_PIPE) return 0;
      fail("output pipe failed");
    }
    if (!available) return 0;
    if (!ReadFile(handle, buffer, DWORD(std::min<size_t>(capacity,available)), &received, nullptr)) fail("output pipe failed");
    return received;
  }
  bool exited(int &code) {
    DWORD status = 0;
    if (!GetExitCodeProcess(process, &status)) fail("process status failed");
    code = int(status);
    return status != STILL_ACTIVE;
  }
  void idle() { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
};
#else
struct Child {
  int input = -1, output = -1, errors = -1;
  pid_t pid = -1;
  ~Child() {
    if (pid > 0) { kill(-pid, SIGKILL); kill(pid, SIGKILL); while(waitpid(pid,nullptr,0)<0 && errno==EINTR){} }
    for (int descriptor : {input,output,errors}) if(descriptor>=0) close(descriptor);
  }
  void launch(const fs::path &runtime, const fs::path &home, const fs::path &sqliteHome) {
    int in[2]{-1,-1}, out[2]{-1,-1}, err[2]{-1,-1};
    struct Ends { int *a,*b,*c; ~Ends(){for(auto pair:{a,b,c})for(int i=0;i<2;++i)if(pair[i]>=0)close(pair[i]);} } ends{in,out,err};
    if(pipe(in)||pipe(out)||pipe(err))fail("pipe creation failed");
    for (auto pair : {in,out,err}) for(int i=0;i<2;++i) if(fcntl(pair[i],F_SETFD,FD_CLOEXEC)<0)fail("pipe setup failed");
    posix_spawn_file_actions_t actions;
    if(posix_spawn_file_actions_init(&actions))fail("process setup failed");
    struct Actions { posix_spawn_file_actions_t *value; ~Actions(){posix_spawn_file_actions_destroy(value);} } releaseActions{&actions};
    for(auto mapping : {std::pair<int,int>{in[0],STDIN_FILENO},{out[1],STDOUT_FILENO},{err[1],STDERR_FILENO}})
      if(posix_spawn_file_actions_adddup2(&actions,mapping.first,mapping.second))fail("process setup failed");
    for(auto pair:{in,out,err})for(int i=0;i<2;++i)if(posix_spawn_file_actions_addclose(&actions,pair[i]))fail("process setup failed");
    posix_spawnattr_t attributes;
    if(posix_spawnattr_init(&attributes))fail("process setup failed");
    struct Attributes { posix_spawnattr_t *value; ~Attributes(){posix_spawnattr_destroy(value);} } releaseAttributes{&attributes};
    short flags=POSIX_SPAWN_SETPGROUP;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    if(posix_spawnattr_setflags(&attributes,flags)||posix_spawnattr_setpgroup(&attributes,0))fail("process setup failed");
    std::vector<std::string> variables;
    for(char **item=environ;item && *item;++item)
      if(strncmp(*item,"CODEX_HOME=",11) && strncmp(*item,"CODEX_SQLITE_HOME=",18))variables.emplace_back(*item);
    variables.emplace_back("CODEX_HOME="+utf8(home)); variables.emplace_back("CODEX_SQLITE_HOME="+utf8(sqliteHome));
    std::vector<char*> env;
    for(auto &item:variables)env.push_back(item.data());env.push_back(nullptr);
    std::string executable=utf8(runtime), argument="app-server";
    char *arguments[]{executable.data(),argument.data(),nullptr};
    if(posix_spawn(&pid,executable.c_str(),&actions,&attributes,arguments,env.data())) {pid=-1;fail("launch failed");}
    input=in[1];in[1]=-1;output=out[0];out[0]=-1;errors=err[0];err[0]=-1;
    for(int descriptor:{input,output,errors})if(fcntl(descriptor,F_SETFL,O_NONBLOCK)<0)fail("pipe setup failed");
#ifdef F_SETNOSIGPIPE
    if(fcntl(input,F_SETNOSIGPIPE,1)<0)fail("pipe setup failed");
#endif
  }
  void send(const std::string &data) {
    if(data.size()>16384)fail("request too large");
    // Requests are bounded below a fresh pipe's capacity and sent only after a reply.
    size_t offset=0;
    while(offset<data.size()) {
      auto count=write(input,data.data()+offset,data.size()-offset);
      if(count<0 && errno==EINTR)continue;
      if(count<=0)fail("input pipe failed");
      offset+=size_t(count);
    }
  }
  size_t read(int descriptor,char *buffer,size_t capacity) {
    auto count=::read(descriptor,buffer,capacity);
    if(count<0 && (errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    if(count<0)fail("output pipe failed");
    return size_t(count);
  }
  bool exited(int &code) {
    int status=0;auto result=waitpid(pid,&status,WNOHANG);
    if(result==0 || (result<0 && errno==EINTR))return false;
    if(result<0)fail("process status failed");
    code=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
    // Keep the group alive until destruction to terminate any helper descendants.
    kill(-pid,SIGKILL);pid=-1;return true;
  }
  void idle() { pollfd descriptors[]{{output,POLLIN,0},{errors,POLLIN,0}};poll(descriptors,2,5); }
};
#endif
} // namespace

std::filesystem::path locate_runtime() {
  std::vector<fs::path> candidates;
#ifdef _WIN32
  auto local=environment(L"LOCALAPPDATA");
  if(!local.empty()) {
    auto bin=fs::path(local)/L"OpenAI"/L"Codex"/L"bin";
    std::vector<std::pair<fs::file_time_type,fs::path>> versions;
    std::error_code error;
    for(fs::directory_iterator i(bin,error),end;!error && i!=end;i.increment(error)) {
      auto executable=i->path()/L"codex.exe";
      std::error_code modifiedError;
      if(regular(executable)) {
        auto modified=fs::last_write_time(executable,modifiedError);
        if(!modifiedError)versions.emplace_back(modified,executable);
      }
    }
    std::sort(versions.begin(),versions.end(),[](const auto &a,const auto &b){return a.first>b.first;});
    // Desktop updates keep their runtime in a versioned directory. The loose
    // executable can be an older installation that rejects current settings.
    for(const auto &version:versions)candidates.push_back(version.second);
    candidates.push_back(bin/L"codex.exe");
  }
  auto path=environment(L"PATH");
  size_t start=0;
  for(size_t end=0;start<=path.size();start=end+1) {
    end=path.find(L';',start);if(end==std::wstring::npos)end=path.size();
    auto part=path.substr(start,end-start);
    if(part.size()>1 && part.front()==L'"' && part.back()==L'"')part=part.substr(1,part.size()-2);
    if(!part.empty())candidates.push_back(fs::path(part)/L"codex.exe");
    if(end==path.size())break;
  }
#else
  candidates.emplace_back("/Applications/Codex.app/Contents/Resources/codex");
  if(auto home=std::getenv("HOME"))candidates.push_back(fs::path(home)/"Applications/Codex.app/Contents/Resources/codex");
  std::string path=std::getenv("PATH")?std::getenv("PATH"):"";
  size_t start=0;
  for(size_t end=0;start<=path.size();start=end+1) {
    end=path.find(':',start);if(end==std::string::npos)end=path.size();
    if(end>start)candidates.push_back(fs::path(path.substr(start,end-start))/"codex");
    if(end==path.size())break;
  }
#endif
  for(const auto &candidate:candidates)if(regular(candidate)) {
#ifndef _WIN32
    if(access(candidate.c_str(),X_OK)!=0)continue;
#endif
    std::error_code error;auto result=fs::absolute(candidate,error);
    if(!error)return result;
  }
  fail("installed runtime not found");
}

std::string probe_config(const std::filesystem::path &runtime,
                         const std::filesystem::path &home,
                         const std::filesystem::path &cwd) {
  if(!runtime.is_absolute() || !home.is_absolute() || !cwd.is_absolute())fail("absolute paths required");
  if(!regular(runtime))fail("runtime unavailable");
  const auto deadline=Clock::now()+std::chrono::seconds(20);
  ProbeStorage storage;
  Child child;child.launch(runtime,home,storage.path);
  child.send(Json({{"id",1},{"method","initialize"},{"params",{{"clientInfo",{{"name","yilai_switcher"},{"version","3.3.6"}}},{"capabilities",{{"experimentalApi",true}}}}}}).dump()+"\n");
  bool initialized=false;
  std::string pending;
  size_t received=0;
  for(;;) {
    if(Clock::now()>=deadline)fail("timed out after 20 seconds");
    char buffer[32768];
    auto count=child.read(child.output,buffer,sizeof(buffer));
    received+=count; if(received>outputLimit)fail("output limit exceeded");
    pending.append(buffer,count);
    auto errors=child.read(child.errors,buffer,sizeof(buffer));
    received+=errors;if(received>outputLimit)fail("output limit exceeded");
    for(size_t newline;(newline=pending.find('\n'))!=std::string::npos;) {
      auto line=pending.substr(0,newline);pending.erase(0,newline+1);
      if(line.empty())continue;
      auto message=Json::parse(line,nullptr,false);
      if(message.is_discarded() || !message.is_object())fail("invalid protocol response");
      if(!message.contains("id") || !message["id"].is_number_integer())continue;
      auto id=message["id"].get<int>();
      if((!initialized && id!=1)||(initialized && id!=2))continue;
      if(message.contains("error")) {
        const auto &error=message["error"];
        const auto code=error.is_object() && error.contains("code") && error["code"].is_number_integer()
          ? std::to_string(error["code"].get<int>()) : std::string("unknown");
        const auto detail=error.is_object() && error.contains("message") && error["message"].is_string()
          ? error["message"].get<std::string>() : std::string();
        // Never forward raw server errors: they can quote config values/keys.
        std::string reason=initialized ? "Codex 拒绝读取配置" : "Codex 初始化被拒绝";
        if(code=="-32601")reason="当前 Codex 运行时不支持所需配置读取接口";
        else if(detail.find("unknown variant")!=std::string::npos && detail.find("model_reasoning_effort")!=std::string::npos)
          reason="当前 Codex 运行时不支持配置中的推理档位，请使用与桌面应用匹配的 Codex 版本";
        else if(detail.find("Model provider")!=std::string::npos && detail.find("not found")!=std::string::npos)
          reason="配置引用了未定义的模型供应商";
        else if(detail.find("invalid configuration")!=std::string::npos)
          reason="配置字段不被当前 Codex 运行时接受";
        else if(code=="-32602")reason="配置读取参数与当前 Codex 运行时不兼容";
        throw std::runtime_error(reason+"（RPC "+code+"）。运行时："+utf8(runtime)+"；配置上下文："+utf8(cwd));
      }
      if(!message.contains("result") || !message["result"].is_object())fail("missing protocol result");
      if(initialized)return message["result"].dump();
      initialized=true;
      auto notification=Json({{"method","initialized"},{"params",Json::object()}}).dump();
      auto request=Json({{"id",2},{"method","config/read"},{"params",{{"includeLayers",true},{"cwd",utf8(cwd)}}}}).dump();
      child.send(notification+"\n"+request+"\n");
    }
    int code=0;
    // A process can exit after writing a reply. Drain any immediately readable data
    // before reporting its exit, so the final response is not lost.
    if(!count && !errors && child.exited(code))throw std::runtime_error("Codex config probe: process exited ("+std::to_string(code)+")");
    if(!count && !errors)child.idle();
  }
}
} // namespace yilai_sources
