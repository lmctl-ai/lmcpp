#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <lmcpp/lmcpp.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cerrno>
#include <cwchar>
#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace lmcpp {
namespace {
struct Handle {
  HANDLE value = nullptr;
  explicit Handle(HANDLE h = nullptr) : value(h) {}
  ~Handle() { reset(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  void reset() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (!n) throw std::invalid_argument("child arguments/environment must be UTF-8");
  std::wstring out(n, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}
// Microsoft CRT argv quoting; no command interpreter or shell expansion.
std::wstring quote(const std::wstring& s) {
  std::wstring out = L"\""; size_t slashes = 0;
  for (wchar_t c : s) {
    if (c == L'\\') { ++slashes; continue; }
    out.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
    out += c; slashes = 0;
  }
  out.append(2 * slashes, L'\\'); return out + L'"';
}
struct Attributes {
  std::vector<unsigned char> bytes;
  LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
  Attributes() {
    SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    bytes.resize(size); list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) throw std::runtime_error("child handle-list initialization failed");
  }
  ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
};
}
ChildResult run_child(const std::vector<std::string>& argv, const std::map<std::string,std::string>& env,
                      std::chrono::milliseconds timeout, std::size_t max_capture) {
  if (argv.empty() || argv[0].empty()) throw std::invalid_argument("run_child requires argv[0]");
  for (const auto& arg : argv) if (arg.find('\0') != std::string::npos) throw std::invalid_argument("argv contains NUL");
  std::vector<std::pair<std::wstring,std::wstring>> entries;
  std::wstring path;bool path_supplied=false;
  for (const auto& item : env) {
    if (item.first.empty() || item.first.find_first_of(std::string("=\0",2)) != std::string::npos || item.second.find('\0') != std::string::npos)
      throw std::invalid_argument("invalid environment entry");
    entries.emplace_back(wide(item.first), wide(item.second));
    if (_wcsicmp(entries.back().first.c_str(), L"PATH") == 0) {path = entries.back().second;path_supplied=true;}
  }
  auto less = [](const auto& a, const auto& b) { return CompareStringOrdinal(a.first.c_str(), -1, b.first.c_str(), -1, TRUE) == CSTR_LESS_THAN; };
  std::sort(entries.begin(), entries.end(), less);
  std::wstring block;
  for (size_t i=0;i<entries.size();++i) {
    if (i && !less(entries[i-1], entries[i])) throw std::invalid_argument("duplicate case-insensitive environment name");
    block += entries[i].first + L"=" + entries[i].second; block += L'\0';
  }
  block += L'\0'; if (entries.empty()) block += L'\0';
  std::wstring executable = wide(argv[0]), command;
  for (const auto& a : argv) { if (!command.empty()) command += L' '; command += quote(wide(a)); }
  if (executable.find_first_of(L"/\\:") == std::wstring::npos) {
    if (!path_supplied) { wchar_t system[MAX_PATH+1]; UINT n=GetSystemDirectoryW(system,MAX_PATH+1); if(n && n<=MAX_PATH)path.assign(system,n); }
    std::wstring name=executable; if(std::filesystem::path(name).extension().empty())name+=L".exe";
    bool found=false;size_t begin=0;
    do { size_t end=path.find(L';',begin);auto dir=path.substr(begin,end==std::wstring::npos?end:end-begin);
      auto candidate=(std::filesystem::path(dir.empty()?L".":dir)/name).wstring();DWORD attr=GetFileAttributesW(candidate.c_str());
      if(attr!=INVALID_FILE_ATTRIBUTES && !(attr&FILE_ATTRIBUTE_DIRECTORY)){executable=candidate;found=true;break;}
      begin=end==std::wstring::npos?end:end+1;
    }while(begin!=std::wstring::npos);
    if(!found){ChildResult r;r.exec_failed=true;r.exec_errno=ENOENT;r.exec_error="executable not found in supplied PATH";r.exit_code=127;return r;}
  }
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  Handle out_read,out_write,err_read,err_write;
  if(!CreatePipe(&out_read.value,&out_write.value,&security,0) || !CreatePipe(&err_read.value,&err_write.value,&security,0) ||
     !SetHandleInformation(out_read.value,HANDLE_FLAG_INHERIT,0) || !SetHandleInformation(err_read.value,HANDLE_FLAG_INHERIT,0))
    throw std::runtime_error("child capture pipe failed");
  Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
  if(input.value==INVALID_HANDLE_VALUE)throw std::runtime_error("child null input failed");
  Handle job(CreateJobObjectW(nullptr,nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!job.value || !SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("child job initialization failed");
  Attributes attributes;HANDLE inherited[]={input.value,out_write.value,err_write.value};
  if(!UpdateProcThreadAttribute(attributes.list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))throw std::runtime_error("child handle allowlist failed");
  STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput=input.value;startup.StartupInfo.hStdOutput=out_write.value;startup.StartupInfo.hStdError=err_write.value;startup.lpAttributeList=attributes.list;
  PROCESS_INFORMATION info{};ChildResult result;
  if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,block.data(),nullptr,&startup.StartupInfo,&info)){
    DWORD code=GetLastError();result.exec_failed=true;result.exec_errno=(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)?ENOENT:code==ERROR_ACCESS_DENIED?EACCES:EINVAL;
    result.exec_error=std::system_category().message(code);result.exit_code=127;return result;
  }
  Handle process(info.hProcess),thread(info.hThread);
  // Assign before the first instruction, so children cannot escape the lifetime job.
  if(!AssignProcessToJobObject(job.value,process.value) || ResumeThread(thread.value)==DWORD(-1)){
    DWORD error=GetLastError();TerminateProcess(process.value,127);WaitForSingleObject(process.value,1000);
    throw std::runtime_error("child job assignment/resume failed: "+std::system_category().message(error));
  }
  out_write.reset();err_write.reset();input.reset();thread.reset();
  auto drain=[&](Handle& pipe,std::string& target,bool& truncated){
    for(int turn=0;turn<16 && pipe.value;++turn){DWORD available=0;
      if(!PeekNamedPipe(pipe.value,nullptr,0,nullptr,&available,nullptr)){pipe.reset();break;}
      if(!available)break;char buf[4096];DWORD read=0;
      if(!ReadFile(pipe.value,buf,std::min<DWORD>(available,sizeof(buf)),&read,nullptr)||!read){pipe.reset();break;}
      size_t take=std::min<size_t>(read,target.size()<max_capture?max_capture-target.size():0);target.append(buf,take);truncated|=take<read;
    }
  };
  auto deadline=std::chrono::steady_clock::now()+timeout;bool done=false;
  while(!done||out_read.value||err_read.value){
    drain(out_read,result.out,result.truncated_out);drain(err_read,result.err,result.truncated_err);
    done=WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;
    if(done&&!out_read.value&&!err_read.value)break;
    if(std::chrono::steady_clock::now()>=deadline){result.timed_out=true;TerminateJobObject(job.value,1);
      auto grace=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
      do{drain(out_read,result.out,result.truncated_out);drain(err_read,result.err,result.truncated_err);Sleep(1);}while((out_read.value||err_read.value)&&std::chrono::steady_clock::now()<grace);
      break;
    }
    if(!done)WaitForSingleObject(process.value,10);else Sleep(10);
  }
  DWORD status=0;
  if(WaitForSingleObject(process.value,100)==WAIT_OBJECT_0 && GetExitCodeProcess(process.value,&status))result.exit_code=static_cast<int>(status);
  else{result.status_unknown=true;result.status_error="child exit status unavailable";}
  return result;
}
} // namespace lmcpp
