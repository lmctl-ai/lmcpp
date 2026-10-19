#include <lmcpp/lmcpp.hpp>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
using namespace lmcpp;
int main(int argc,char** argv){
  assert(argc==2);std::string fixture=argv[1];std::map<std::string,std::string> env{{"LMCPP_KEEP","kept"}};
#ifdef _WIN32
  if(auto root=std::getenv("SystemRoot"))env["SystemRoot"]=root;
  if(auto path=std::getenv("PATH"))env["PATH"]=path;
  _putenv_s("LMCPP_LEAK","must not inherit");
#else
  setenv("LMCPP_LEAK","must not inherit",1);
#endif
  const std::vector<std::string> values={"", "a b", "quote\"here", "trailing\\", "two\\\\\"slashes", "&|<>;$()", "utf8-\xc3\xa9"};
  std::vector<std::string> args={fixture,"args"};args.insert(args.end(),values.begin(),values.end());
  auto echoed=run_child(args,env,std::chrono::seconds(5));assert(echoed.exit_code==0&&!echoed.exec_failed);assert(Json::parse(echoed.out)==values);
  auto spaced=std::filesystem::path(fixture).parent_path()/"child fixture with spaces";
#ifdef _WIN32
  spaced+=".exe";
#endif
  std::filesystem::copy_file(fixture,spaced,std::filesystem::copy_options::overwrite_existing);
  args[0]=spaced.string();auto space_result=run_child(args,env,std::chrono::seconds(5));assert(space_result.exit_code==0&&Json::parse(space_result.out)==values);std::filesystem::remove(spaced);
  auto isolated=run_child({fixture,"env"},env,std::chrono::seconds(5));assert(isolated.exit_code==0);auto e=Json::parse(isolated.out);assert(e["keep"]=="kept"&&e["leak"]==false);
  auto io=run_child({fixture,"stdio"},env,std::chrono::seconds(5));assert(io.exit_code==0&&io.out=="EOF"&&io.err=="error");
  auto flood=run_child({fixture,"flood"},env,std::chrono::seconds(5),4096);assert(flood.exit_code==0&&flood.out.size()==4096&&flood.err.size()==4096&&flood.truncated_out&&flood.truncated_err);
  auto timed=run_child({fixture,"sleep"},env,std::chrono::milliseconds(200));assert(timed.timed_out);
  auto tree=run_child({fixture,"descendant"},env,std::chrono::milliseconds(500));assert(tree.timed_out&&!tree.out.empty());
#ifdef _WIN32
  HANDLE child=OpenProcess(SYNCHRONIZE,FALSE,std::stoul(tree.out));if(child){assert(WaitForSingleObject(child,1000)==WAIT_OBJECT_0);CloseHandle(child);}
#endif
  auto missing=run_child({fixture+"-missing"},env,std::chrono::seconds(5));assert(missing.exec_failed&&!missing.exec_error.empty());
  auto exited=run_child({fixture,"exit"},env,std::chrono::seconds(5));assert(!exited.exec_failed&&exited.exit_code==127);
  // Native PATH lookup must use the supplied environment, never an implicit shell.
#ifdef _WIN32
  env["Path"]=std::filesystem::path(fixture).parent_path().string()+";"+env["PATH"];env.erase("PATH");
#else
  env["PATH"]=std::filesystem::path(fixture).parent_path().string();
#endif
  auto searched=run_child({std::filesystem::path(fixture).filename().string(),"exit"},env,std::chrono::seconds(5));assert(!searched.exec_failed&&searched.exit_code==127);
  std::string file;{Context ctx;file=ctx.temp_file_path();{std::ofstream out(std::filesystem::u8path(file),std::ios::binary);out<<"a\r\nb\n";}assert(read_result_file(file,"text")["text"]=="a\r\nb\n");}assert(!std::filesystem::exists(std::filesystem::u8path(file)));
  std::cout<<"portable argv/env/stdio/capture/timeout/tree/PATH/temp checks passed\n";
}
