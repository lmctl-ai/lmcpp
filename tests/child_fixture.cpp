#include <lmcpp/lmcpp.hpp>
#include <iostream>
#include <filesystem>
#include <thread>
#include <fstream>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#ifdef _WIN32
int wmain(int argc,wchar_t** wide_argv){
  std::vector<std::string> storage;std::vector<char*> pointers;
  for(int i=0;i<argc;++i){int n=WideCharToMultiByte(CP_UTF8,0,wide_argv[i],-1,nullptr,0,nullptr,nullptr);std::string text(n,0);WideCharToMultiByte(CP_UTF8,0,wide_argv[i],-1,text.data(),n,nullptr,nullptr);text.pop_back();storage.push_back(text);}
  for(auto& text:storage)pointers.push_back(text.data());char** argv=pointers.data();
#else
int main(int argc,char** argv){
#endif
#ifdef _WIN32
  _setmode(_fileno(stdout),_O_BINARY);_setmode(_fileno(stderr),_O_BINARY);
#endif
  std::string mode=argc>1?argv[1]:"";
  if(mode=="args"){std::vector<std::string> args;for(int i=2;i<argc;++i)args.push_back(argv[i]);std::cout<<lmcpp::Json(args).dump();}
  else if(mode=="env"){const char* keep=std::getenv("LMCPP_KEEP");std::cout<<lmcpp::Json{{"keep",keep?keep:""},{"leak",std::getenv("LMCPP_LEAK")!=nullptr}}.dump();}
  else if(mode=="stdio"){char c;bool eof=!std::cin.get(c);std::cout<<(eof?"EOF":"inherited");std::cerr<<"error";}
  else if(mode=="flood"){for(int i=0;i<10000;++i){std::cout<<"0123456789abcdef";std::cerr<<"fedcba9876543210";}}
  else if(mode=="sleep")std::this_thread::sleep_for(std::chrono::seconds(10));
  else if(mode=="exit")return 127;
  else if(mode=="descendant"){
#ifdef _WIN32
    std::wstring command=L"\""+std::filesystem::u8path(argv[0]).wstring()+L"\" sleep";
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 2;
    std::cout<<pi.dwProcessId<<std::endl;CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
#else
    pid_t pid=fork();if(pid<0)return 2;if(pid==0){std::this_thread::sleep_for(std::chrono::seconds(10));_exit(0);}std::cout<<pid<<std::endl;
#endif
  }
  return 0;
}
