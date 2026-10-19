#include "lmcpp/lmcpp.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <fstream>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <chrono>
#include <map>
#include <cstring>
#include <limits>
#include <sys/resource.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
#endif
#include <set>
#include <cerrno>
#include <cstring>
#include <limits>

namespace lmcpp {
#ifndef _WIN32
ChildResult run_child(const std::vector<std::string>& argv,
                      const std::map<std::string,std::string>& env,
                      std::chrono::milliseconds timeout,
                      std::size_t max_capture) {
  if (argv.empty() || argv[0].empty()) throw std::invalid_argument("run_child requires argv[0]");
  for (const auto& arg : argv) if (arg.find('\0') != std::string::npos) throw std::invalid_argument("argv contains NUL");
  for (const auto& item : env) {
    if (item.first.empty() || item.first.find('=') != std::string::npos || item.first.find('\0') != std::string::npos || item.second.find('\0') != std::string::npos)
      throw std::invalid_argument("invalid environment entry");
  }

  // Build every allocation-bearing object before fork: the child uses only async-signal-safe calls.
  std::vector<std::string> environment;
  std::string search_path = "/bin:/usr/bin";
  for (const auto& item : env) {
    environment.push_back(item.first + "=" + item.second);
    if (item.first == "PATH") search_path = item.second;
  }
  std::vector<char*> envp;
  for (auto& item : environment) envp.push_back(item.data());
  envp.push_back(nullptr);
  std::string executable = argv[0];
  if (executable.find('/') == std::string::npos) {
    std::string candidate;
    bool found = false;
    std::size_t begin = 0;
    do {
      std::size_t end = search_path.find(':', begin);
      std::string dir = search_path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
      candidate = (dir.empty() ? std::string(".") : dir) + "/" + executable;
      if (access(candidate.c_str(), X_OK) == 0) { executable = candidate; found = true; break; }
      begin = end == std::string::npos ? end : end + 1;
    } while (begin != std::string::npos);
    if (!found && !candidate.empty()) executable = candidate; // execve reports the precise final errno.
  }
  std::vector<char*> child_argv;
  for (const auto& arg : argv) child_argv.push_back(const_cast<char*>(arg.c_str()));
  child_argv.push_back(nullptr);

  auto make_pipe = [](int p[2]) {
#ifdef __linux__
    if (pipe2(p, O_CLOEXEC) == 0) return true;
    if (errno != ENOSYS) return false;
#endif
    if (pipe(p) != 0) return false;
    for (int fd : {p[0], p[1]}) {
      int flags = fcntl(fd, F_GETFD);
      if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) { int e=errno; close(p[0]); close(p[1]); errno=e; return false; }
    }
    return true;
  };
  int out_pipe[2], err_pipe[2], status_pipe[2];
  if (!make_pipe(out_pipe)) throw std::runtime_error("stdout pipe failed");
  if (!make_pipe(err_pipe)) { close(out_pipe[0]); close(out_pipe[1]); throw std::runtime_error("stderr pipe failed"); }
  if (!make_pipe(status_pipe)) { close(out_pipe[0]);close(out_pipe[1]);close(err_pipe[0]);close(err_pipe[1]);throw std::runtime_error("exec status pipe failed"); }
  int null_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
  if (null_fd < 0) { for(int fd:{out_pipe[0],out_pipe[1],err_pipe[0],err_pipe[1],status_pipe[0],status_pipe[1]})close(fd); throw std::runtime_error("open /dev/null failed"); }
  // Pipes must live above stdio even if the caller closed one of its standard descriptors.
  auto lift_fd = [](int& fd) { if(fd >= 3) return true; int moved=fcntl(fd,F_DUPFD_CLOEXEC,3);if(moved<0)return false;close(fd);fd=moved;return true; };
  for (int* fd : {&out_pipe[0],&out_pipe[1],&err_pipe[0],&err_pipe[1],&status_pipe[0],&status_pipe[1],&null_fd}) {
    if (!lift_fd(*fd)) { int e=errno;for(int* x:{&out_pipe[0],&out_pipe[1],&err_pipe[0],&err_pipe[1],&status_pipe[0],&status_pipe[1],&null_fd})if(*x>=0)close(*x);errno=e;throw std::runtime_error("normalize child descriptors failed"); }
  }
  long open_max = sysconf(_SC_OPEN_MAX);
  if (open_max < 0 || open_max > 1048576) open_max = 65536;
  const pid_t parent_pid = getpid();
  pid_t pid = fork();
  if (pid < 0) { int e=errno;for(int fd:{out_pipe[0],out_pipe[1],err_pipe[0],err_pipe[1],status_pipe[0],status_pipe[1],null_fd})close(fd);errno=e;throw std::runtime_error("fork failed"); }
  if (pid == 0) {
    setpgid(0, 0);
    auto child_fail = [&](int e) { (void)!write(status_pipe[1], &e, sizeof(e)); _exit(127); };
#ifdef __linux__
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) child_fail(errno);
    if (getppid() != parent_pid) _exit(127);
#endif
    if (dup2(null_fd, STDIN_FILENO) < 0 || dup2(out_pipe[1], STDOUT_FILENO) < 0 || dup2(err_pipe[1], STDERR_FILENO) < 0) child_fail(errno);
    sigset_t empty; sigemptyset(&empty); if(sigprocmask(SIG_SETMASK,&empty,nullptr)<0)child_fail(errno);
    struct sigaction action{}; action.sa_handler=SIG_DFL; sigemptyset(&action.sa_mask);
    for(int sig=1;sig<NSIG;++sig) { if(sig==SIGKILL||sig==SIGSTOP)continue; if(sigaction(sig,&action,nullptr)<0&&errno!=EINVAL)child_fail(errno); }
    bool closed = false;
#if defined(__linux__) && defined(SYS_close_range)
    int saved_errno=errno;
    bool a=(status_pipe[1]<=3)||syscall(SYS_close_range,3u,static_cast<unsigned>(status_pipe[1]-1),0u)==0;
    bool b=syscall(SYS_close_range,static_cast<unsigned>(status_pipe[1]+1),std::numeric_limits<unsigned>::max(),0u)==0;
    closed=a&&b;
    if(!closed)errno=saved_errno;
#endif
    if(!closed)for(int fd=3;fd<open_max;++fd)if(fd!=status_pipe[1])close(fd);
    execve(executable.c_str(),child_argv.data(),envp.data());
    child_fail(errno);
  }

  setpgid(pid, pid);
  close(out_pipe[1]); close(err_pipe[1]); close(status_pipe[1]); close(null_fd);
  for(int fd:{out_pipe[0],err_pipe[0],status_pipe[0]})fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
  ChildResult result;
  bool out_open=true,err_open=true,status_open=true,child_done=false;
  int status=0; bool have_status=false;
  const auto deadline=std::chrono::steady_clock::now()+timeout;
  auto close_read=[](int&fd,bool&open){if(open){close(fd);open=false;}};
  auto drain=[&](int fd,std::string&target,bool&open,bool&truncated){char buffer[4096];for(;;){ssize_t n=read(fd,buffer,sizeof(buffer));if(n>0){std::size_t room=target.size()<max_capture?max_capture-target.size():0;std::size_t take=std::min(static_cast<std::size_t>(n),room);target.append(buffer,take);if(take<static_cast<std::size_t>(n))truncated=true;}else if(n==0){close(fd);open=false;break;}else if(errno==EAGAIN||errno==EWOULDBLOCK)break;else if(errno!=EINTR){close(fd);open=false;break;}}};
  auto reap=[&](){if(child_done)return;pid_t waited=waitpid(pid,&status,WNOHANG);if(waited==pid){child_done=true;have_status=true;}else if(waited<0&&errno!=EINTR){child_done=true;result.status_unknown=true;result.status_error=strerror(errno);}};
  auto read_exec_status=[&](){if(!status_open)return;int e=0;ssize_t n=read(status_pipe[0],&e,sizeof(e));if(n==static_cast<ssize_t>(sizeof(e))){result.exec_failed=true;result.exec_errno=e;result.exec_error=strerror(e);close_read(status_pipe[0],status_open);}else if(n==0){close_read(status_pipe[0],status_open);}else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)close_read(status_pipe[0],status_open);};
  while(out_open||err_open||status_open||!child_done){
    pollfd fds[3]={{out_open?out_pipe[0]:-1,static_cast<short>(out_open?POLLIN|POLLHUP:0),0},{err_open?err_pipe[0]:-1,static_cast<short>(err_open?POLLIN|POLLHUP:0),0},{status_open?status_pipe[0]:-1,static_cast<short>(status_open?POLLIN|POLLHUP:0),0}};
    auto now=std::chrono::steady_clock::now();
    auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now).count();
    int wait_ms=static_cast<int>(std::max<long long>(0,std::min<long long>(100,left)));
    int ready; do { ready=poll(fds,3,wait_ms); } while(ready<0&&errno==EINTR&&std::chrono::steady_clock::now()<deadline);
    if(ready>0&&out_open&&(fds[0].revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL)))drain(out_pipe[0],result.out,out_open,result.truncated_out);
    if(ready>0&&err_open&&(fds[1].revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL)))drain(err_pipe[0],result.err,err_open,result.truncated_err);
    if(ready>0&&status_open&&(fds[2].revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL)))read_exec_status();
    reap();
    if(std::chrono::steady_clock::now()>=deadline&&(out_open||err_open||status_open||!child_done)){
      result.timed_out=true;kill(-pid,SIGKILL);
      const auto grace=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
      while(std::chrono::steady_clock::now()<grace&&(out_open||err_open||status_open||!child_done)){
        pollfd gfds[3]={{out_pipe[0],static_cast<short>(out_open?POLLIN|POLLHUP:0),0},{err_pipe[0],static_cast<short>(err_open?POLLIN|POLLHUP:0),0},{status_pipe[0],static_cast<short>(status_open?POLLIN|POLLHUP:0),0}};poll(gfds,3,10);
        if(out_open)drain(out_pipe[0],result.out,out_open,result.truncated_out);if(err_open)drain(err_pipe[0],result.err,err_open,result.truncated_err);if(status_open)read_exec_status();reap();
      }
      close_read(out_pipe[0],out_open);close_read(err_pipe[0],err_open);close_read(status_pipe[0],status_open);
      if(!child_done){while(waitpid(pid,&status,0)<0){if(errno==EINTR)continue;result.status_unknown=true;result.status_error=strerror(errno);break;}if(!result.status_unknown)have_status=true;child_done=true;}
      break;
    }
  }
  if(have_status){if(WIFEXITED(status))result.exit_code=WEXITSTATUS(status);else if(WIFSIGNALED(status)){result.signal=WTERMSIG(status);result.exit_code=128+result.signal;}}
  return result;
}
#endif
static const char* type_name(Type t) { switch(t){case Type::String:return "string";case Type::Number:case Type::Integer:return "number";case Type::Boolean:return "boolean";case Type::Object:return "object";case Type::Array:return "array";case Type::Json:return "json";} return "string"; }
static int exit_for(ErrorCode c) { switch(c){case ErrorCode::InvalidRequest:case ErrorCode::ValidationError:return 2;case ErrorCode::ParseError:return 3;case ErrorCode::NotFound:return 4;case ErrorCode::Conflict:return 5;case ErrorCode::Forbidden:case ErrorCode::ReadOnlyViolation:return 6;case ErrorCode::Timeout:return 7;case ErrorCode::ServiceUnavailable:return 8;case ErrorCode::MissingConfirmation:return 9;case ErrorCode::UnsupportedProductVersion:return 10;default:return 1;} }
static std::string code_name(ErrorCode c) { switch(c){case ErrorCode::InvalidRequest:return "INVALID_REQUEST";case ErrorCode::ValidationError:return "VALIDATION_ERROR";case ErrorCode::ParseError:return "PARSE_ERROR";case ErrorCode::NotFound:return "NOT_FOUND";case ErrorCode::Conflict:return "CONFLICT";case ErrorCode::Forbidden:return "FORBIDDEN";case ErrorCode::ReadOnlyViolation:return "READONLY_VIOLATION";case ErrorCode::Timeout:return "TIMEOUT";case ErrorCode::ServiceUnavailable:return "SERVICE_UNAVAILABLE";case ErrorCode::MissingConfirmation:return "MISSING_CONFIRMATION";case ErrorCode::UnsupportedProductVersion:return "UNSUPPORTED_PRODUCT_VERSION";case ErrorCode::InternalError:return "INTERNAL_ERROR";case ErrorCode::CommandFailed:return "COMMAND_FAILED";} return "UNKNOWN_ERROR"; }
static std::string safe_dump(const Json&j,int indent=-1) noexcept {try{return j.dump(indent,' ',false,Json::error_handler_t::replace);}catch(...){return "{\"success\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"serialization failed\",\"data\":{}}}";}}
Context::Context()=default;
Context::~Context(){if(!path_.empty()){std::error_code error;std::filesystem::remove(std::filesystem::u8path(path_),error);}}
const std::string& Context::temp_file_path(){if(path_.empty()){
#ifdef _WIN32
  wchar_t directory[MAX_PATH+1],file[MAX_PATH+1];DWORD length=GetTempPathW(MAX_PATH+1,directory);
  if(!length||length>MAX_PATH||!GetTempFileNameW(directory,L"lmc",0,file))throw std::runtime_error("cannot create temporary result file");
  path_=std::filesystem::path(file).u8string();
#else
  std::string pattern=(std::filesystem::temp_directory_path()/"lmcpp-result-XXXXXX").string();
  int fd=::mkstemp(pattern.data());if(fd<0)throw std::runtime_error("cannot create temporary result file");::close(fd);path_=pattern;
#endif
}return path_;}
#ifdef _WIN32
static std::string read_regular_bounded(const std::string& path,size_t cap,ErrorCode code,const std::string& kind){
  std::filesystem::path native;try{native=std::filesystem::u8path(path);}catch(...){throw Error{code,"cannot read "+kind+": "+path};}
  HANDLE file=CreateFileW(native.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)throw Error{code,"cannot read "+kind+": "+path};
  struct Closer{HANDLE h;~Closer(){CloseHandle(h);}} closer{file};BY_HANDLE_FILE_INFORMATION info{};
  if(GetFileType(file)!=FILE_TYPE_DISK||!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw Error{code,kind+" must be a regular file: "+path};
  std::string data;char buf[8192];while(data.size()<=cap){DWORD n=0;DWORD want=static_cast<DWORD>(std::min(sizeof(buf),cap+1-data.size()));
    if(!ReadFile(file,buf,want,&n,nullptr))throw Error{code,"cannot read "+kind+": "+path};if(!n)break;data.append(buf,n);}
  if(data.size()>cap){bool output=kind=="result file";throw Error{output?ErrorCode::CommandFailed:ErrorCode::ParseError,kind+" exceeds 16777216 bytes",{{"tool_code",output?"OUTPUT_TOO_LARGE":"INPUT_TOO_LARGE"},{"limit",cap}}};}return data;
}
#else
static std::string read_regular_bounded(const std::string&path,size_t cap,ErrorCode code,const std::string&kind){int fd=::open(path.c_str(),O_RDONLY|O_NONBLOCK);if(fd<0)throw Error{code,"cannot read "+kind+": "+path};struct stat st{};if(::fstat(fd,&st)!=0||!S_ISREG(st.st_mode)){::close(fd);throw Error{code,kind+" must be a regular file: "+path};}std::string data;char buf[8192];while(data.size()<=cap){size_t want=std::min(sizeof(buf),cap+1-data.size());ssize_t n=::read(fd,buf,want);if(n<0&&errno==EINTR)continue;if(n<0){::close(fd);throw Error{code,"cannot read "+kind+": "+path};}if(n==0)break;data.append(buf,static_cast<size_t>(n));}::close(fd);if(data.size()>cap){bool output=kind=="result file";throw Error{output?ErrorCode::CommandFailed:ErrorCode::ParseError,kind+" exceeds 16777216 bytes",{{"tool_code",output?"OUTPUT_TOO_LARGE":"INPUT_TOO_LARGE"},{"limit",cap}}};}return data;}
#endif
Json read_result_file(const std::string&path,const std::string&format){constexpr size_t cap=16*1024*1024;if(format!="json"&&format!="text")throw Error{ErrorCode::InvalidRequest,"unsupported result file format: "+format};std::string data=read_regular_bounded(path,cap,ErrorCode::CommandFailed,"result file");if(format=="text")return Json{{"text",data}};try{return Json::parse(data);}catch(...){throw Error{ErrorCode::CommandFailed,"result file contains malformed JSON",{{"tool_code","INVALID_RESULT_JSON"}}};}}
static Json param_json(const Param&p){Json j={{"name",p.name},{"type",type_name(p.type)},{"occurs",p.occurs}};if(!p.default_value.is_null())j["default"]=p.default_value;if(!p.enum_values.empty())j["enum"]=p.enum_values;if(!p.description.empty())j["description"]=p.description;if(p.type==Type::Array)j["items"]=type_name(p.item_type);if(!p.aliases.empty())j["aliases"]=p.aliases;if(p.positional)j["positional"]=true;return j;}
Json publish_schema(const CliSpec& s) { Json ops=Json::array(); for(const auto& o:s.operations){Json ps=Json::array();for(const auto&p:o.params)ps.push_back(param_json(p));ops.push_back({{"name",o.name},{"access",o.access==Access::Read?"read":"write"},{"returns",o.returns},{"description",o.description},{"params",ps}});} return {{"schema","cli.schema.v1"},{"tool",s.tool},{"version",s.version},{"inputs",{"flags","json"}},{"globals",Json::array()},{"operations",ops}}; }
static const Operation& find_op(const CliSpec&s,const std::string& n){for(const auto&o:s.operations)if(o.name==n)return o;throw Error{ErrorCode::InvalidRequest,"unknown operation: "+n,{{"op",n}}};}
static bool value_ok(const Json&v,Type t){switch(t){case Type::String:return v.is_string();case Type::Number:return v.is_number();case Type::Integer:return v.is_number_integer();case Type::Boolean:return v.is_boolean();case Type::Object:return v.is_object();case Type::Array:return v.is_array();case Type::Json:return true;}return false;}
static bool special_number(const Json&v){return v.is_null()||(v.is_string()&&(v=="NA"||v=="NaN"||v=="Inf"||v=="-Inf"));}
static Json validate(const Operation&o,const Json& raw,const std::string& form="json"){Json result=Json::object(), issues=Json::array();if(!raw.is_object())throw Error{ErrorCode::ValidationError,"input params must be an object"};for(auto it=raw.begin();it!=raw.end();++it){bool known=false;for(auto&p:o.params)known|=p.name==it.key();if(!known)issues.push_back({{"code","UNKNOWN_PARAM"},{"param",it.key()},{"message","unknown parameter: "+it.key()}});} for(const auto&p:o.params){auto it=raw.find(p.name);if(it==raw.end()){if((p.occurs=="1"||p.occurs=="+")&&p.default_value.is_null())issues.push_back({{"code","MISSING_REQUIRED"},{"param",p.name},{"message","missing required parameter: "+p.name}});else if(!p.default_value.is_null())result[p.name]=p.default_value;continue;}if((form=="flags"&&p.doc_only)||(form=="json"&&p.cli_only)){issues.push_back({{"code","FORM_NOT_ALLOWED"},{"param",p.name},{"message","parameter is not allowed in this input form"}});continue;}const Json&v=*it;bool multi=p.occurs=="+"||p.occurs=="*"||p.type==Type::Array;bool ok=true;Json normalized=v;if(multi){if(v.is_array())normalized=v;else normalized=Json::array({v});Type elem=p.type==Type::Array?p.item_type:p.type;for(const auto&item:normalized)if(!value_ok(item,elem)&&!(p.allow_special_numbers&&elem==Type::Number&&special_number(item)))ok=false;if(p.occurs=="+"&&normalized.empty()){issues.push_back({{"code","MISSING_REQUIRED"},{"param",p.name},{"message","missing required parameter: "+p.name}});continue;}}else{ok=p.type==Type::Integer?v.is_number_integer():value_ok(v,p.type);if(p.allow_special_numbers&&p.type==Type::Number&&special_number(v))ok=true;if(p.type==Type::String&&(v.is_number()||v.is_boolean())){normalized=v.is_boolean()?(v.get<bool>()?"true":"false"):v.dump();ok=true;}}if(!ok){issues.push_back({{"code","TYPE_MISMATCH"},{"param",p.name},{"message","invalid type for parameter: "+p.name}});continue;}if(!p.enum_values.empty()){bool en=true;if(multi){for(auto&x:normalized)en&=std::find(p.enum_values.begin(),p.enum_values.end(),x)!=p.enum_values.end();}else en=std::find(p.enum_values.begin(),p.enum_values.end(),normalized)!=p.enum_values.end();if(!en){issues.push_back({{"code","ENUM_VIOLATION"},{"param",p.name},{"message","invalid enum value for parameter: "+p.name}});continue;}}result[p.name]=normalized;}if(!issues.empty())throw Error{ErrorCode::ValidationError,"invalid arguments for \""+o.name+"\"",{{"op",o.name},{"issues",issues}}};return result;}
Json validate_params(const Operation&o,const Json&raw,const std::string&form){return validate(o,raw,form);}
static void check_json_depth(const std::string&t){size_t d=0;bool q=false,e=false;for(char c:t){if(q){if(e)e=false;else if(c=='\\')e=true;else if(c=='"')q=false;continue;}if(c=='"'){q=true;continue;}if(c=='{'||c=='['){if(++d>128)throw Error{ErrorCode::ValidationError,"JSON input exceeds maximum nesting depth 128",{{"issues",Json::array({Json{{"code","DOCUMENT_PARSE"},{"message","JSON input exceeds maximum nesting depth 128"}}})}}};}else if((c=='}'||c==']')&&d)--d;}}
static Json parse_limited(const std::string&t){check_json_depth(t);try{return Json::parse(t);}catch(const Error&){throw;}catch(...){throw Error{ErrorCode::ParseError,"malformed JSON input"};}}
static Json get_document_params(const Json&doc,std::string&op){if(!doc.is_object())throw Error{ErrorCode::ValidationError,"JSON input must be an object"};bool envelope=doc.contains("op")&&doc["op"].is_string();if(envelope)for(auto it=doc.begin();it!=doc.end();++it)if(it.key()!="op"&&it.key()!="params"){envelope=false;break;}if(envelope){op=doc["op"].get<std::string>();if(!doc.contains("params"))return Json::object();if(!doc["params"].is_object())throw Error{ErrorCode::ValidationError,"params in JSON envelope must be an object"};return doc["params"];}return doc;}
static std::vector<Param> globals(){return {Param{"dryrun",Type::Boolean,false,nullptr,{},"","?",false,false,false,{"dry-run"}},Param{"readonly",Type::Boolean,false,nullptr,{},"","?",false,false,false,{"read-only"}},Param{"confirm",Type::Boolean,false,nullptr,{},"","?",false,false,false,{"i-understand"}},Param{"input-json",Type::String,false,nullptr,{},"","?",false,false,false,{"inputJson"}},Param{"input-file",Type::String,false,nullptr,{},"","?",false,false,false,{"inputFile"}},Param{"format",Type::String,false,nullptr,{},"","?"}};}
Resolved resolve_input(const CliSpec&s,const std::vector<std::string>&a){if(a.empty())throw Error{ErrorCode::InvalidRequest,"missing operation name"};const Operation*o=nullptr;for(const auto&x:s.operations)if(x.name==a[0])o=&x;if(!o)throw Error{ErrorCode::InvalidRequest,"unknown operation: "+a[0],{{"op",a[0]}}};std::vector<std::string>tail(a.begin()+1,a.end());Json parsed=parse_argv(*o,tail,globals()),g=parsed["globals"];if(g.contains("input-json")||g.contains("input-file")){if(!parsed["params"].empty()||!parsed["positionals"].empty())throw Error{ErrorCode::ValidationError,"cannot mix document input with operation flags",{{"issues",Json::array({Json{{"code","MIXED_INPUT"},{"message","cannot mix document input with operation flags"}}})}}};if(g.contains("input-json")&&g.contains("input-file"))throw Error{ErrorCode::ValidationError,"multiple document inputs",{{"issues",Json::array({Json{{"code","MIXED_INPUT"},{"message","multiple document inputs"}}})}}};std::string text=g.contains("input-json")?g["input-json"].get<std::string>():read_regular_bounded(g["input-file"].get<std::string>(),16*1024*1024,ErrorCode::ParseError,"JSON input file");std::string docop;Json p=get_document_params(parse_limited(text),docop);if(!docop.empty()&&docop!=o->name)throw Error{ErrorCode::ValidationError,"operation mismatch",{{"issues",Json::array({Json{{"code","OP_MISMATCH"},{"op",docop},{"message","operation mismatch"}}})}}};return{o->name,validate(*o,p,"json")};}return{o->name,parsed["params"]};}
Json parse_argv(const Operation&o,const std::vector<std::string>&argv,const std::vector<Param>&globalspec){Json raw=Json::object(),g=Json::object(),pos=Json::array(),issues=Json::array();std::vector<const Param*> slots;for(const auto&p:o.params)if(p.positional)slots.push_back(&p);size_t pi=0;bool after=false;auto keb=[](std::string x){std::replace(x.begin(),x.end(),'_','-');return x;};auto find=[&](const std::string&n,const std::vector<Param>&v)->const Param*{for(const auto&p:v)if(n==p.name||n==keb(p.name)||std::find(p.aliases.begin(),p.aliases.end(),n)!=p.aliases.end())return &p;return nullptr;};auto short_alias=[&](const std::string&t){if(t.size()!=2||t[0]!='-')return false;std::string alias=t.substr(1);for(const auto&p:o.params)if(std::find(p.aliases.begin(),p.aliases.end(),alias)!=p.aliases.end())return true;return false;};auto issue=[&](std::string c,std::string m,std::string p=""){Json x={{"code",c},{"message",m}};if(!p.empty())x["param"]=p;issues.push_back(x);};
for(size_t i=0;i<argv.size();++i){std::string tok=argv[i];if(tok=="--"&&!after){after=true;continue;}if(after||(tok.rfind("--",0)!=0&&!short_alias(tok))){pos.push_back(tok);if(pi<slots.size()){const Param&p=*slots[pi];Type t=p.type==Type::Array?p.item_type:p.type;Json value=tok;if(t!=Type::String){try{if(p.allow_special_numbers&&(tok=="NA"||tok=="NaN"||tok=="Inf"||tok=="-Inf"))value=tok;else{value=parse_limited(tok);if(t==Type::Integer&&!value.is_number_integer())throw 1;if(t==Type::Number&&!value.is_number())throw 1;if(t==Type::Boolean&&!value.is_boolean())throw 1;if(t==Type::Object&&!value.is_object())throw 1;if(t==Type::Array&&!value.is_array())throw 1;}}catch(...){issue("TYPE_MISMATCH","invalid positional value",p.name);continue;}}if(p.occurs=="+"||p.occurs=="*"||p.type==Type::Array){if(!raw.contains(p.name))raw[p.name]=Json::array();raw[p.name].push_back(value);}else{raw[p.name]=value;++pi;}}else issue("EXTRA_POSITIONAL","extra positional argument");continue;}
size_t eq=tok.find('=');size_t prefix=tok.rfind("--",0)==0?2:1;std::string key=tok.substr(prefix,eq==std::string::npos?std::string::npos:eq-prefix);bool neg=false,global=false;const Param*p=find(key,o.params);if(!p&&key.rfind("no-",0)==0){const Param*c=find(key.substr(3),o.params);if(c&&c->type==Type::Boolean){p=c;neg=true;}}if(!p){p=find(key,globalspec);global=p!=nullptr;}if(!p&&key.rfind("no-",0)==0){const Param*c=find(key.substr(3),globalspec);if(c&&c->type==Type::Boolean){p=c;global=true;neg=true;}}if(!p){std::string identity=key;std::replace(identity.begin(),identity.end(),'-','_');issue("UNKNOWN_PARAM","unknown flag: --"+key,identity);if(eq==std::string::npos&&i+1<argv.size()&&argv[i+1].rfind("--",0)!=0)++i;continue;}Json&dest=global?g:raw;bool multi=p->occurs=="+"||p->occurs=="*"||p->type==Type::Array;if(dest.contains(p->name)&&!multi){issue("DUPLICATE_FLAG","duplicate flag: --"+key,p->name);if(eq==std::string::npos&&p->type!=Type::Boolean&&i+1<argv.size()&&argv[i+1].rfind("--",0)!=0)++i;continue;}Type value_type=p->type==Type::Array?p->item_type:p->type;Json v;if(value_type==Type::Boolean&&eq==std::string::npos&&i+1<argv.size()&&(argv[i+1]=="true"||argv[i+1]=="false")){v=argv[++i]=="true";}else if(value_type==Type::Boolean&&eq==std::string::npos)v=!neg;else if(neg){issue("TYPE_MISMATCH","negated flag takes no value",p->name);continue;}else{std::string value;if(eq!=std::string::npos)value=tok.substr(eq+1);else if(i+1<argv.size()&&argv[i+1].rfind("--",0)!=0)value=argv[++i];else{issue("MISSING_VALUE","missing value for --"+key,p->name);continue;}if(value_type==Type::String)v=value;else if(p->allow_special_numbers&&(value=="NA"||value=="NaN"||value=="Inf"||value=="-Inf"))v=value;else try{v=parse_limited(value);if(value_type==Type::Boolean&&!v.is_boolean())throw 1;if(value_type==Type::Integer&&!v.is_number_integer())throw 1;if(value_type==Type::Number&&!v.is_number())throw 1;if(value_type==Type::Object&&!v.is_object())throw 1;if(value_type==Type::Array&&!v.is_array())throw 1;}catch(...){issue("TYPE_MISMATCH","invalid value for --"+key,p->name);continue;}}if(multi){if(!dest.contains(p->name))dest[p->name]=Json::array();dest[p->name].push_back(v);}else dest[p->name]=v;}
try{raw=validate(o,raw,"flags");}catch(const Error&e){if(e.data.contains("issues"))for(const auto&i:e.data["issues"])issues.push_back(i);else issue("VALIDATION_ERROR",e.message);}if(!issues.empty())throw Error{ErrorCode::ValidationError,"invalid argv for \""+o.name+"\"",{{"op",o.name},{"issues",issues}}};return{{"params",raw},{"positionals",pos},{"globals",g}};}
Json parse_published_schema(const Json&d){Json issues=Json::array();auto add=[&](std::string code,std::string param="",std::string op=""){Json x={{"code",code},{"message",code}};if(!param.empty())x["param"]=param;if(!op.empty())x["op"]=op;issues.push_back(x);};const std::vector<std::string> reserved={"__proto__","constructor","prototype"};auto checkparam=[&](const Json&p,std::string op,bool global){if(!p.is_object()){add("TYPE_MISMATCH","",op);return;}std::string n=p.value("name","");if(std::find(reserved.begin(),reserved.end(),n)!=reserved.end())add("RESERVED_NAME",n,op);static const std::vector<std::string> allowed={"name","type","occurs","default","enum","positional","items","description","aliases","cliOnly","docOnly","derived"};for(auto it=p.begin();it!=p.end();++it)if(std::find(allowed.begin(),allowed.end(),it.key())==allowed.end())add("UNKNOWN_PARAM",p.value("name",it.key()),op);if(!global&&n=="dryrun")add("DOCUMENT_PARSE",n,op);std::string t=p.value("type","");static const std::vector<std::string> types={"string","number","boolean","object","array","json"};if(std::find(types.begin(),types.end(),t)==types.end())add("ENUM_VIOLATION",n,op);std::string occ=p.value("occurs","1");if(occ!="1"&&occ!="?"&&occ!="+"&&occ!="*")add("ENUM_VIOLATION",n,op);if(p.contains("default")){const Json&v=p["default"];bool multi=occ=="+"||occ=="*"||t=="array";auto match=[&](const Json&x){if(p.contains("enum")&&std::find(p["enum"].begin(),p["enum"].end(),x)==p["enum"].end())return false;if(t=="string")return x.is_string();if(t=="number")return x.is_number();if(t=="boolean")return x.is_boolean();if(t=="object")return x.is_object();if(t=="array"){std::string item=p.value("items","string");if(item=="string")return x.is_string();if(item=="number")return x.is_number();if(item=="boolean")return x.is_boolean();if(item=="object")return x.is_object();return false;}return true;};bool ok=true;if(multi){ok=v.is_array();if(ok)for(auto&x:v)ok&=match(x);}else ok=match(v);if(!ok)add("TYPE_MISMATCH",n,op);}if(p.contains("aliases")&&p["aliases"].is_array())for(auto&a:p["aliases"])if(a.is_string()&&std::find(reserved.begin(),reserved.end(),a.get<std::string>())!=reserved.end())add("RESERVED_NAME",n,op);};
if(!d.is_object()||d.value("schema","")!="cli.schema.v1")add("DOCUMENT_PARSE");std::string tool=d.value("tool","");if(std::find(reserved.begin(),reserved.end(),tool)!=reserved.end())add("RESERVED_NAME",tool);std::set<std::string> names;if(d.contains("operations")&&d["operations"].is_array())for(auto&o:d["operations"]){std::string n=o.value("name","");if(std::find(reserved.begin(),reserved.end(),n)!=reserved.end())add("RESERVED_NAME",n,n);if(!names.insert(n).second)add("DOCUMENT_PARSE","",n);if(o.contains("access")&&o["access"]!="read"&&o["access"]!="write"&&o["access"]!="dynamic")add("ENUM_VIOLATION","",n);if(o.contains("returns")&&o["returns"]!="object"&&o["returns"]!="array"&&o["returns"]!="string"&&o["returns"]!="number"&&o["returns"]!="boolean"&&o["returns"]!="null")add("ENUM_VIOLATION","",n);if(o.contains("params")&&o["params"].is_array())for(auto&p:o["params"])checkparam(p,n,false);}if(d.contains("globals")&&d["globals"].is_array())for(auto&p:d["globals"])checkparam(p,"",true);
if(d.contains("product")){const auto&p=d["product"];static const std::vector<std::string> keys={"name","supported","detect"};for(auto it=p.begin();it!=p.end();++it)if(std::find(keys.begin(),keys.end(),it.key())==keys.end())add("UNKNOWN_PARAM",it.key());if(p.value("name","").empty())add("DOCUMENT_PARSE");std::string supported=p.value("supported","");if(supported.empty()||supported.find('^')!=std::string::npos)add("DOCUMENT_PARSE");if(p.contains("detect")){auto x=p["detect"];std::string pat=x.value("pattern","");int groups=0;bool esc=false;for(char c:pat){if(esc){esc=false;continue;}if(c=='\\'){esc=true;continue;}if(c=='(')++groups;}if(!x.contains("argv")||!x["argv"].is_array()||x["argv"].empty()||groups!=1||pat.size()>200)add("DOCUMENT_PARSE");}}
return{{"ok",issues.empty()},{"issues",issues}};}
static bool env_on(const std::string& tool,const char* suffix){std::string key;for(unsigned char c:tool)key+=std::isalnum(c)?static_cast<char>(std::toupper(c)):'_';key+="_";key+=suffix;const char*v=std::getenv(key.c_str());if(!v)return false;std::string x(v);for(auto&c:x)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return x=="1"||x=="true"||x=="yes"||x=="on";}
int run_cli_io(const CliSpec&s,const std::vector<std::string>&args,std::ostream&out,std::ostream&err){auto emit=[&](Json v,int code){out<<safe_dump(v,2)<<'\n';return code;};try{
  if(args.empty()||args.front()=="--help"||args.front()=="-h"){out<<s.tool<<" operations:\n";for(const auto&o:s.operations)out<<"  "<<o.name<<" - "<<o.description<<"\n";return 0;}
bool schema=false,only_policy=true;for(const auto&t:args){if(t=="--schema")schema=true;else if(t!="--readonly"&&t!="--read-only"&&t!="--dryrun"&&t!="--dry-run"&&t!="--confirm"&&t!="--i-understand")only_policy=false;}if(schema&&only_policy){out<<safe_dump(publish_schema(s),2)<<'\n';return 0;}
if(args.size()==1&&args[0]=="--list"){Json list=Json::array();for(const auto&o:s.operations)list.push_back({{"name",o.name},{"access",o.access==Access::Read?"read":"write"},{"description",o.description}});out<<safe_dump(list)<<'\n';return 0;}
auto explain=args.begin();if(!args.empty()&&args.front()=="--explain"){if(explain+1==args.end()||(explain+1)->rfind("--",0)==0)return emit(Json{{"success",false},{"error",{{"code","INVALID_REQUEST"},{"message","Missing operation name for --explain"},{"data",Json::object()}}}},2);const Operation*o=nullptr;for(const auto&x:s.operations)if(x.name==*(explain+1))o=&x;if(!o)return emit(Json{{"success",false},{"error",{{"code","INVALID_REQUEST"},{"message","Unknown operation for --explain: "+*(explain+1)},{"data",Json::object()}}}},2);Json doc={{"name",o->name},{"access",o->access==Access::Read?"read":"write"},{"returns",o->returns},{"description",o->description},{"params",Json::array()}};for(const auto&p:o->params)doc["params"].push_back(param_json(p));out<<safe_dump(doc,2)<<'\n';return 0;}
const Operation*op=nullptr;size_t opidx=args.size();for(size_t i=0;i<args.size();++i)if(args[i].rfind("--",0)!=0){for(const auto&x:s.operations)if(x.name==args[i]){op=&x;opidx=i;break;}if(op)break;}
std::vector<std::string>route=args;bool inferred=false;
if(!op){inferred=true;if(!args.empty()&&args.front().rfind("--",0)!=0)throw Error{ErrorCode::InvalidRequest,"Unknown operation: "+args.front(),{{"op",args.front()}}};Operation dummy{"<root>",Access::Read,"","object",{}};Json root=parse_argv(dummy,args,globals()),g=root["globals"];std::string text;if(g.contains("input-json"))text=g["input-json"].get<std::string>();else if(g.contains("input-file"))text=read_regular_bounded(g["input-file"].get<std::string>(),16*1024*1024,ErrorCode::ParseError,"JSON input file");else throw Error{ErrorCode::InvalidRequest,"No operation specified"};std::string name;get_document_params(parse_limited(text),name);if(name.empty())throw Error{ErrorCode::InvalidRequest,"No operation specified"};for(const auto&x:s.operations)if(x.name==name)op=&x;if(!op)throw Error{ErrorCode::InvalidRequest,"Unknown operation: "+name,{{"op",name}}};route.insert(route.begin(),name);opidx=route.size();}
if(!inferred&&opidx+1<args.size()&&(args[opidx+1]=="--help"||args[opidx+1]=="-h")){out<<op->name<<" parameters:\n";for(const auto&p:op->params)out<<"  --"<<p.name<<(p.type==Type::Boolean?"":" <value>")<<"\n";return 0;}std::vector<std::string>rest;for(size_t i=0;i<route.size();++i)if(!(i==opidx&&opidx<route.size()))rest.push_back(route[i]);if(inferred)rest.erase(rest.begin());
bool hasdoc=std::find(rest.begin(),rest.end(),"--input-json")!=rest.end()||std::find(rest.begin(),rest.end(),"--input-file")!=rest.end();Json parsed;if(hasdoc){Operation dummy{"<document>",Access::Read,"","object",{}};try{parsed=parse_argv(dummy,rest,globals());}catch(const Error&e){bool duplicate=false;if(e.data.contains("issues")&&e.data["issues"].is_array())for(const auto&i:e.data["issues"])duplicate|=i.value("code","")=="DUPLICATE_FLAG";if(duplicate)throw;throw Error{ErrorCode::ValidationError,"cannot mix document input with operation flags",{{"op",op->name},{"issues",Json::array({Json{{"code","MIXED_INPUT"},{"op",op->name},{"message","cannot mix document input with operation flags"}}})}}};}}else parsed=parse_argv(*op,rest,globals());Json g=parsed["globals"],params=parsed["params"];bool hasj=g.contains("input-json"),hasf=g.contains("input-file");if(hasj&&hasf)throw Error{ErrorCode::ValidationError,"multiple document inputs",{{"issues",Json::array({Json{{"code","MIXED_INPUT"},{"message","multiple document inputs"}}})}}};if(hasj||hasf){if(!params.empty()||!parsed["positionals"].empty())throw Error{ErrorCode::ValidationError,"cannot mix document input with operation flags",{{"op",op->name},{"issues",Json::array({Json{{"code","MIXED_INPUT"},{"op",op->name},{"message","cannot mix document input with operation flags"}}})}}};std::string text=hasj?g["input-json"].get<std::string>():read_regular_bounded(g["input-file"].get<std::string>(),16*1024*1024,ErrorCode::ParseError,"JSON input file");std::string name;Json raw=get_document_params(parse_limited(text),name);if(!name.empty()&&name!=op->name)throw Error{ErrorCode::ValidationError,"operation mismatch",{{"op",name},{"issues",Json::array({Json{{"code","OP_MISMATCH"},{"op",name},{"message","operation mismatch"}}})}}};params=validate(*op,raw,"json");}
bool dry=g.value("dryrun",false)||env_on(s.tool,"DRYRUN"),readonly=g.value("readonly",false)||env_on(s.tool,"READONLY"),confirm=g.value("confirm",false);if(op->access==Access::Write){if(readonly)throw Error{ErrorCode::ReadOnlyViolation,"Operation \""+op->name+"\" is a write operation and this tool is running in read-only mode.",{{"operation",op->name}}};if(dry){out<<safe_dump(Json{{"success",true},{"dryRun",true},{"simulated",op->name}})<<'\n';return 0;}if(op->dangerous&&!confirm)throw Error{ErrorCode::MissingConfirmation,"Operation requires --confirm",{{"operation",op->name}}};}
if(!op->handler&&!op->context_handler)throw Error{ErrorCode::InternalError,"operation has no handler"};Context ctx;Json result=op->context_handler?op->context_handler(params,ctx):op->handler(params);out<<safe_dump(result)<<'\n';return 0;
}catch(const Error&e){return emit(Json{{"success",false},{"error",{{"code",code_name(e.code)},{"message",e.message},{"data",e.data}}}},exit_for(e.code));}catch(const std::exception&e){err<<"lmcpp: internal error\n";return emit(Json{{"success",false},{"error",{{"code","INTERNAL_ERROR"},{"message",e.what()},{"data",Json::object()}}}},1);}catch(...){out<<"{\"success\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"unknown exception\",\"data\":{}}}\n";return 1;}}
int run_cli(const CliSpec&s,int argc,char**argv){std::vector<std::string>a;for(int i=1;i<argc;++i)a.emplace_back(argv[i]);return run_cli_io(s,a,std::cout,std::cerr);}
}
