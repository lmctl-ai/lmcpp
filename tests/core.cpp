#include <lmcpp/lmcpp.hpp>
#include <cassert>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cerrno>
#ifdef _WIN32
#include <io.h>
#include <cstdlib>
#define unlink _unlink
#define access _access
#define setenv(k,v,overwrite) _putenv_s(k,v)
#define unsetenv(k) _putenv_s(k,"")
#define F_OK 0
#else
#include <fcntl.h>
#include <cerrno>
#include <sys/wait.h>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <signal.h>
#endif
using namespace lmcpp;
static std::string test_temp(const char* name){return (std::filesystem::temp_directory_path()/name).string();}
static CliSpec spec(){CliSpec s{"t","1",{}};Operation o{"add",Access::Read,"", "object",{{"a",Type::Number,true},{"b",Type::Number,true}}};o.handler=[](const Params&p){return Json{{"value",p.at("a").get<int>()+p.at("b").get<int>()}};};s.operations.push_back(o);Operation en{"enum",Access::Read,"","object",{{"x",Type::String,true,nullptr,{"a","b"}}}};en.handler=[](const Params&p){return p;};s.operations.push_back(en);return s;}
static void array_regression(){Operation arr{"array",Access::Read,"","object",{Param{"p",Type::Array,true,nullptr,{},"","",false,false,false,{},Type::Number}}};arr.handler=[](const Params&p){return p;};auto schema=publish_schema(CliSpec{"arrays","1",{arr}});assert(schema["operations"][0]["params"][0]["items"]=="number");assert(parse_published_schema(schema)["ok"]==true);auto one=parse_argv(arr,{"--p","0.975"});assert(one["params"]["p"]==Json::array({0.975}));auto repeated=parse_argv(arr,{"--p=0.975","--p","0.5"});assert(repeated["params"]["p"]==Json::array({0.975,0.5}));bool json_array_rejected=false;try{parse_argv(arr,{"--p","[0.975]"});}catch(const Error&e){json_array_rejected=e.code==ErrorCode::ValidationError;}assert(json_array_rejected);arr.handler=[](const Params&p){return p.at("p");};std::ostringstream out,err;CliSpec spec{"arrays","1",{arr}};assert(run_cli_io(spec,{"array","--p=0.975","--p","0.5"},out,err)==0);assert(Json::parse(out.str())==Json::array({0.975,0.5}));out.str("");assert(run_cli_io(spec,{"array","--p","[0.975]"},out,err)==2);arr.params[0].allow_special_numbers=true;auto special=parse_argv(arr,{"--p","NA","--p=NaN","--p","Inf","--p","-Inf"});assert(special["params"]["p"]==Json::array({"NA","NaN","Inf","-Inf"}));Json doc={{"p",Json::array({nullptr,"NA","NaN","Inf","-Inf"})}};assert(validate_params(arr,doc,"json")["p"].size()==5);out.str("");assert(run_cli_io(CliSpec{"arrays","1",{arr}},{"array","--p","NA"},out,err)==0);assert(Json::parse(out.str())==Json::array({"NA"}));}
int main(){array_regression();auto s=spec();auto schema=publish_schema(s);assert(schema==publish_schema(s));assert(schema["schema"]=="cli.schema.v1");std::ostringstream out,err;assert(run_cli_io(s,{"add","--a","2","--b=3"},out,err)==0);assert(Json::parse(out.str())["value"]==5);out.str("");assert(run_cli_io(s,{"add","--input-json","{\"op\":\"add\",\"params\":{\"a\":2,\"b\":3}}"},out,err)==0);assert(Json::parse(out.str())["value"]==5);
  Operation textop{"text",Access::Read,"","object",{{"text",Type::String,false,nullptr,{},"","?"}}};textop.handler=[](const Params&p){return Json{{"text",p.value("text","")}};};s.operations.push_back(textop);out.str("");assert(run_cli_io(s,{"text","--text","1.50"},out,err)==0);assert(Json::parse(out.str())["text"]=="1.50");out.str("");assert(run_cli_io(s,{"text"},out,err)==0);assert(Json::parse(out.str())["text"]=="");
  for(auto args:std::vector<std::vector<std::string>>{{"add","--wat","1"},{"add","--a","x","--b","2"},{"add","--a","1"},{"enum","--x","z"},{"add","--a","1","--a","2","--b","3"}}){out.str("");assert(run_cli_io(s,args,out,err)==2);auto e=Json::parse(out.str());assert(e["success"]==false);assert(!e["error"]["message"].get<std::string>().empty());}
  out.str("");assert(run_cli_io(s,{"--schema"},out,err)==0);assert(Json::parse(out.str())==publish_schema(s));
  Operation boom{"boom",Access::Read,"","object",{}};boom.handler=[](const Params&)->Json{throw std::runtime_error("boom");};s.operations.push_back(boom);out.str("");assert(run_cli_io(s,{"boom"},out,err)==1);assert(Json::parse(out.str())["error"]["code"]=="INTERNAL_ERROR");
  std::string inp=test_temp("lmcpp-test-input.json");{std::ofstream f(inp);f<<"{\"a\":4,\"b\":6}";}out.str("");assert(run_cli_io(s,{"add","--input-file",inp},out,err)==0);assert(Json::parse(out.str())["value"]==10);::unlink(inp.c_str());
  Operation write{"note",Access::Write,"","object",{{"path",Type::String,true},{"text",Type::String,true}}};write.handler=[](const Params&p){std::ofstream f(p.at("path").get<std::string>(),std::ios::app);f<<p.at("text");return Json{{"written",true}};};s.operations.push_back(write);std::string note=test_temp("lmcpp-note-policy-test");::unlink(note.c_str());out.str("");assert(run_cli_io(s,{"note","--path",note,"--text","blocked","--readonly"},out,err)==6);assert(Json::parse(out.str())["error"]["code"]=="READONLY_VIOLATION");assert(::access(note.c_str(),F_OK)!=0);out.str("");assert(run_cli_io(s,{"note","--path",note,"--text","planned","--dryrun"},out,err)==0);assert(Json::parse(out.str())["dryRun"]==true);assert(::access(note.c_str(),F_OK)!=0);
  ::setenv("T_READONLY","true",1);out.str("");assert(run_cli_io(s,{"note","--path",note,"--text","blocked"},out,err)==6);::unsetenv("T_READONLY");::setenv("T_DRYRUN","yes",1);out.str("");assert(run_cli_io(s,{"note","--path",note,"--text","planned"},out,err)==0);::unsetenv("T_DRYRUN");assert(::access(note.c_str(),F_OK)!=0);
  Operation danger{"danger",Access::Write,"","object",{}};danger.dangerous=true;danger.handler=[](const Params&){return Json{{"done",true}};};s.operations.push_back(danger);out.str("");assert(run_cli_io(s,{"danger"},out,err)==9);out.str("");assert(run_cli_io(s,{"danger","--i-understand"},out,err)==0);
  Operation repeated{"repeated",Access::Read,"","object",{{"shape",Type::Number,true,nullptr,{},"","+"}}};repeated.handler=[](const Params&p){return p;};s.operations.push_back(repeated);out.str("");assert(run_cli_io(s,{"repeated","--shape","2","--shape","2"},out,err)==0);assert(Json::parse(out.str())["shape"]==Json::array({2,2}));
  std::string badfile=test_temp("lmcpp-bad-input.json");{std::ofstream f(badfile);f<<"{";}out.str("");assert(run_cli_io(s,{"add","--input-file",badfile},out,err)==3);assert(Json::parse(out.str())["error"]["code"]=="PARSE_ERROR");::unlink(badfile.c_str());out.str("");assert(run_cli_io(s,{"add","--input-file",badfile},out,err)==3);::unlink(note.c_str());
  std::string temp;{Context ctx;temp=ctx.temp_file_path();assert(::access(temp.c_str(),F_OK)==0);std::ofstream f(temp);f<<"{\"ok\":true}";f.close();assert(read_result_file(temp,"json")["ok"]==true);assert(read_result_file(temp,"text").is_object());}assert(::access(temp.c_str(),F_OK)!=0);
  std::string huge=test_temp("lmcpp-result-too-large");{std::ofstream f(huge,std::ios::binary);f.seekp(16*1024*1024);f.put('x');}bool capped=false;try{read_result_file(huge,"json");}catch(const Error&e){capped=e.code==ErrorCode::CommandFailed&&e.data["tool_code"]=="OUTPUT_TOO_LARGE";}assert(capped);::unlink(huge.c_str());
  Operation cli{"cli",Access::Read,"","object",{Param{"max_count",Type::Integer,true},Param{"verbose",Type::Boolean,false,nullptr,{},"","?"},Param{"text",Type::String,false,nullptr,{},"","?",false,false,false,{"t"}},Param{"file",Type::String,false,nullptr,{},"","?",true},Param{"index",Type::Integer,false,nullptr,{},"","?",true}}};cli.handler=[](const Params&p){return p;};s.operations.push_back(cli);
  Operation meta{"meta",Access::Read,"","object",{Param{"values",Type::Array,false,nullptr,{},"","*",false,false,false,{"v"},Type::Number},Param{"path",Type::String,false,nullptr,{},"","?",true}}};CliSpec metaspec{"meta_tool","1",{meta}};auto metadoc=publish_schema(metaspec);assert(metadoc["operations"][0]["params"][0]["type"]=="array"&&metadoc["operations"][0]["params"][0]["items"]=="number");assert(metadoc["operations"][0]["params"][0]["aliases"][0]=="v");assert(metadoc["operations"][0]["params"][1]["positional"]==true&&metadoc["operations"][0]["params"][1]["occurs"]=="?");assert(metadoc["operations"][0]["params"][1]["type"]=="string");assert(parse_published_schema(metadoc)["ok"]==true);assert(publish_schema(CliSpec{"int_tool","1",{Operation{"int",Access::Read,"","object",{Param{"count",Type::Integer,true}}}}})["operations"][0]["params"][0]["type"]=="number");
  out.str("");assert(run_cli_io(s,{"cli","--max-count","2","--no-verbose","-t","1.50","--","pos","4"},out,err)==0);auto cliout=Json::parse(out.str());assert(cliout["max_count"]==2&&cliout["verbose"]==false&&cliout["text"]=="1.50"&&cliout["file"]=="pos"&&cliout["index"]==4);
  out.str("");assert(run_cli_io(s,{"cli","--max-count","2","--text","--dry-run"},out,err)==2);assert(Json::parse(out.str())["error"]["data"]["issues"][0]["code"]=="MISSING_VALUE");
  out.str("");assert(run_cli_io(s,{"add","--a","--b","3"},out,err)==2);assert(Json::parse(out.str())["error"]["data"]["issues"][0]["code"]=="MISSING_VALUE");out.str("");assert(run_cli_io(s,{"add","--a","2","--b","3","--input-json","{}"},out,err)==2);assert(Json::parse(out.str())["error"]["data"]["issues"][0]["code"]=="MIXED_INPUT");
  std::string bad="unknown";bad.push_back(static_cast<char>(0xff));out.str("");assert(run_cli_io(s,{bad},out,err)==2);assert(Json::parse(out.str())["error"]["code"]=="INVALID_REQUEST");bad="--";bad.push_back(static_cast<char>(0xff));bad+="x";out.str("");assert(run_cli_io(s,{"add",bad,"1"},out,err)==2);assert(Json::parse(out.str())["success"]==false);
  std::string badpath=test_temp("lmcpp-bad-path-");badpath.push_back(static_cast<char>(0xff));out.str("");assert(run_cli_io(s,{"add","--input-file",badpath},out,err)==3);assert(Json::parse(out.str())["error"]["code"]=="PARSE_ERROR");
  std::string deep(50000,'[');deep+="0";deep+=std::string(50000,']');out.str("");assert(run_cli_io(s,{"add","--input-json",deep},out,err)==2);assert(Json::parse(out.str())["success"]==false);
  out.str("");assert(run_cli_io(s,{"--input-json","{\"op\":\"add\",\"params\":{\"a\":2,\"b\":3}}"},out,err)==0);assert(Json::parse(out.str())["value"]==5);
  out.str("");assert(run_cli_io(s,{"--schema","--readonly"},out,err)==0);assert(Json::parse(out.str())["schema"]=="cli.schema.v1");out.str("");assert(run_cli_io(s,{"--list"},out,err)==0);assert(Json::parse(out.str()).is_array());out.str("");assert(run_cli_io(s,{"--explain","add"},out,err)==0);assert(Json::parse(out.str())["name"]=="add");
  out.str("");assert(run_cli_io(s,{"add","--input-json","{\"op\":\"add\",\"params\":{\"a\":2,\"b\":3},\"extra\":true}"},out,err)==2);assert(Json::parse(out.str())["error"]["data"]["issues"][0]["code"]=="UNKNOWN_PARAM");
  #ifndef _WIN32
  std::string fifo=test_temp("lmcpp-result-fifo");::unlink(fifo.c_str());assert(::mkfifo(fifo.c_str(),0600)==0);alarm(3);bool fifo_rejected=false;try{read_result_file(fifo,"text");}catch(const Error&){fifo_rejected=true;}alarm(0);assert(fifo_rejected);out.str("");alarm(3);assert(run_cli_io(s,{"add","--input-file",fifo},out,err)==3);alarm(0);::unlink(fifo.c_str());alarm(3);bool zero_rejected=false;try{read_result_file("/dev/zero","text");}catch(const Error&){zero_rejected=true;}alarm(0);assert(zero_rejected);out.str("");alarm(3);assert(run_cli_io(s,{"add","--input-file","/dev/zero"},out,err)==3);alarm(0);
  #endif
  std::string huge_input=test_temp("lmcpp-input-too-large");{std::ofstream f(huge_input,std::ios::binary);f.seekp(16*1024*1024);f.put('x');}out.str("");assert(run_cli_io(s,{"add","--input-file",huge_input},out,err)==3);assert(Json::parse(out.str())["error"]["data"]["tool_code"]=="INPUT_TOO_LARGE");::unlink(huge_input.c_str());
  // Slice 8 regressions: argv JSON must have the same depth cap as documents.
  std::string deep_argv(60000,'[');deep_argv+="0";deep_argv+=std::string(60000,']');
  Operation jsonflag{"jsonflag",Access::Read,"","object",{Param{"value",Type::Json,false,nullptr,{},"","?"}}};
  bool deep_flag_rejected=false;try{parse_argv(jsonflag,{"--value",deep_argv});}catch(const Error&e){deep_flag_rejected=e.code==ErrorCode::ValidationError;}assert(deep_flag_rejected);
  Operation jsonpos{"jsonpos",Access::Read,"","object",{Param{"value",Type::Json,false,nullptr,{},"","?",true}}};
  bool deep_pos_rejected=false;try{parse_argv(jsonpos,{deep_argv});}catch(const Error&e){deep_pos_rejected=e.code==ErrorCode::ValidationError;}assert(deep_pos_rejected);
  std::string deep_object;for(int i=0;i<60000;++i)deep_object+="{\"x\":";deep_object+="0";deep_object+=std::string(60000,'}');
  for(Type jt:{Type::Object,Type::Array}){Type item=jt==Type::Array?Type::Number:jt;Operation jf{"deepflag",Access::Read,"","object",{Param{"value",jt,false,nullptr,{},"","?",false,false,false,{},item}}};Operation jp{"deeppos",Access::Read,"","object",{Param{"value",jt,false,nullptr,{},"","?",true,false,false,{},item}}};std::string payload=jt==Type::Object?deep_object:deep_argv;bool rf=false,rp=false;try{parse_argv(jf,{"--value",payload});}catch(const Error&e){rf=e.code==ErrorCode::ValidationError;}try{parse_argv(jp,{payload});}catch(const Error&e){rp=e.code==ErrorCode::ValidationError;}assert(rf&&rp);}
  Operation positional{"pos",Access::Read,"","object",{Param{"n",Type::Number,true,nullptr,{},"","1",true}}};
  auto negpos=parse_argv(positional,{"-5"});assert(negpos["params"]["n"]==-5);
  Operation negval{"negval",Access::Read,"","object",{Param{"n",Type::Number,true}}};
  assert(parse_argv(negval,{"--n","-5"})["params"]["n"]==-5);
  Operation shortalias{"shortalias",Access::Read,"","object",{Param{"value",Type::Number,true,nullptr,{},"","1",false,false,false,{"N"}}}};
  assert(parse_argv(shortalias,{"-N","7"})["params"]["value"]==7);
  Operation paramop{"paramop",Access::Read,"","object",{Param{"a",Type::Number,true},Param{"b",Type::Number,true},Param{"op",Type::String,true}}};
  paramop.handler=[](const Params&p){return p;};s.operations.push_back(paramop);
  out.str("");assert(run_cli_io(s,{"paramop","--input-json",R"({"a":1,"b":2,"op":"add"})"},out,err)==0);
  assert(Json::parse(out.str())["op"]=="add");
  out.str("");assert(run_cli_io(s,{"add","--input-json","{}","--input-json","{}"},out,err)==2);
  assert(Json::parse(out.str())["error"]["data"]["issues"][0]["code"]=="DUPLICATE_FLAG");
  #ifndef _WIN32
  { auto child=run_child({"/bin/sh","-c","printf ok; printf err >&2"},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(2),8); assert(child.exit_code==0&&!child.timed_out&&child.out=="ok"&&child.err=="err"&&!child.truncated_out&&!child.truncated_err); auto capped=run_child({"/bin/sh","-c","printf 123456789"},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(2),4);assert(capped.out=="1234"&&capped.truncated_out); auto timeout=run_child({"/bin/sleep","2"},{{"PATH","/usr/bin:/bin"}},std::chrono::milliseconds(20));assert(timeout.timed_out); auto descendant=run_child({"/bin/sh","-c","sleep 2 & exit 0"},{{"PATH","/usr/bin:/bin"}},std::chrono::milliseconds(20));assert(descendant.timed_out); }
  { int input[2];assert(pipe(input)==0);int saved=dup(STDIN_FILENO);assert(saved>=0);assert(::write(input[1],"inherited\n",10)==10);close(input[1]);dup2(input[0],STDIN_FILENO);close(input[0]);auto c=run_child({"/bin/sh","-c","read x || x=EOF; printf %s \"$x\""},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(2));dup2(saved,STDIN_FILENO);close(saved);assert(c.out=="EOF"); }
  { int fd=open("/dev/null",O_RDONLY);assert(fd>=3);std::string check="test ! -e /dev/fd/"+std::to_string(fd);auto c=run_child({"/bin/sh","-c",check},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(2));close(fd);assert(c.exit_code==0); }
  #ifdef __linux__
  { auto start=std::chrono::steady_clock::now();auto c=run_child({"/bin/sh","-c","setsid sleep 6 & exit 0"},{{"PATH","/usr/bin:/bin"}},std::chrono::milliseconds(300));auto elapsed=std::chrono::steady_clock::now()-start;assert(c.timed_out&&elapsed<std::chrono::seconds(2)); }
  #endif
  { rusage before{},after{};assert(getrusage(RUSAGE_SELF,&before)==0);auto c=run_child({"/bin/sleep","1"},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(3));assert(getrusage(RUSAGE_SELF,&after)==0);double cpu=(after.ru_utime.tv_sec-before.ru_utime.tv_sec)+(after.ru_stime.tv_sec-before.ru_stime.tv_sec)+(after.ru_utime.tv_usec-before.ru_utime.tv_usec)/1e6+(after.ru_stime.tv_usec-before.ru_stime.tv_usec)/1e6;assert(c.exit_code==0&&!c.timed_out&&cpu<0.05); }
  { struct sigaction old_action{},ignored{};ignored.sa_handler=SIG_IGN;sigemptyset(&ignored.sa_mask);sigaction(SIGCHLD,&ignored,&old_action);auto c=run_child({"/bin/sh","-c","exit 3"},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(2));sigaction(SIGCHLD,&old_action,nullptr);assert(c.status_unknown&&c.exit_code==-1&&!c.status_error.empty()); }
  { auto missing=run_child({"/not/a/real/executable"},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(2));assert(missing.exec_failed&&missing.exec_errno==ENOENT&&!missing.exec_error.empty());auto exited=run_child({"/bin/sh","-c","exit 127"},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(2));assert(!exited.exec_failed&&exited.exit_code==127); }
  { struct sigaction old_action{},ignored{};ignored.sa_handler=SIG_IGN;sigemptyset(&ignored.sa_mask);sigaction(SIGPIPE,&ignored,&old_action);auto c=run_child({"/bin/sh","-c","kill -PIPE $$"},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(2));sigaction(SIGPIPE,&old_action,nullptr);assert(c.signal==SIGPIPE&&c.exit_code==128+SIGPIPE); }
  #ifdef __linux__
  { char file[]="/tmp/lmcpp-pdeath-XXXXXX";int fd=mkstemp(file);assert(fd>=0);close(fd);unlink(file);pid_t helper=fork();assert(helper>=0);if(helper==0){std::string cmd="printf '%s' $$ > "+std::string(file)+"; exec /bin/sleep 30";run_child({"/bin/sh","-c",cmd},{{"PATH","/usr/bin:/bin"}},std::chrono::seconds(40));_exit(0);}pid_t target=-1;for(int i=0;i<100&&target<0;++i){std::ifstream f(file);f>>target;usleep(10000);}assert(target>0);kill(helper,SIGKILL);waitpid(helper,nullptr,0);bool dead=false;for(int i=0;i<100&&!dead;++i){if(kill(target,0)<0&&errno==ESRCH)dead=true;else{std::ifstream f(std::string("/proc/")+std::to_string(target)+"/stat");std::string stat;std::getline(f,stat);auto end=stat.rfind(')');dead=end!=std::string::npos&&end+2<stat.size()&&stat[end+2]=='Z';}if(!dead)usleep(10000);}if(!dead)kill(target,SIGKILL);unlink(file);assert(dead); }
#endif
#endif
}
