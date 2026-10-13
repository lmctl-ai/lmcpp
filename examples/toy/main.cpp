#include <lmcpp/lmcpp.hpp>
#include <fstream>
using namespace lmcpp;
int main(int argc,char**argv){
  CliSpec s{"toy","0.1.0",{}};
  Operation add{"add",Access::Read,"Add two numbers","object",{{"a",Type::Number,true},{"b",Type::Number,true}}};
  add.handler=[](const Params&p){return Json{{"value",p.at("a").get<double>()+p.at("b").get<double>()}};};
  Operation scale{"scale",Access::Read,"Scale a number","object",{{"value",Type::Number,true},{"factor",Type::Number,true}}};
  scale.handler=[](const Params&p){return Json{{"value",p.at("value").get<double>()*p.at("factor").get<double>()}};};
  Operation note{"note",Access::Write,"Append a note to a file","object",{{"path",Type::String,true},{"text",Type::String,true}}};
  note.handler=[](const Params&p){std::ofstream f(p.at("path").get<std::string>(),std::ios::app);if(!f)throw std::runtime_error("cannot open note file");f<<p.at("text").get<std::string>()<<'\n';return Json{{"written",true}};};
  s.operations={add,scale,note};return run_cli(s,argc,argv);
}
