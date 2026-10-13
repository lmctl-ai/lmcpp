#include <lmcpp/lmcpp.hpp>
#include <cassert>
#include <sstream>
using namespace lmcpp;
static CliSpec spec(){CliSpec s{"t","1",{}};Operation o{"add",Access::Read,"", "object",{{"a",Type::Number,true},{"b",Type::Number,true}}};o.handler=[](const Params&p){return Json{{"value",p.at("a").get<int>()+p.at("b").get<int>()}};};s.operations.push_back(o);Operation en{"enum",Access::Read,"","object",{{"x",Type::String,true,nullptr,{"a","b"}}}};en.handler=[](const Params&p){return p;};s.operations.push_back(en);return s;}
int main(){auto s=spec();auto schema=publish_schema(s);assert(schema==publish_schema(s));assert(schema["schema"]=="cli.schema.v1");std::ostringstream out,err;assert(run_cli_io(s,{"add","--a","2","--b=3"},out,err)==0);assert(Json::parse(out.str())["value"]==5);out.str("");assert(run_cli_io(s,{"add","--input-json","{\"op\":\"add\",\"params\":{\"a\":2,\"b\":3}}"},out,err)==0);assert(Json::parse(out.str())["value"]==5);
  for(auto args:std::vector<std::vector<std::string>>{{"add","--wat","1"},{"add","--a","x","--b","2"},{"add","--a","1"},{"enum","--x","z"},{"add","--a","1","--a","2","--b","3"}}){out.str("");assert(run_cli_io(s,args,out,err)==2);auto e=Json::parse(out.str());assert(e["success"]==false);assert(!e["error"]["message"].get<std::string>().empty());}
  out.str("");assert(run_cli_io(s,{"--schema"},out,err)==0);assert(Json::parse(out.str())==schema);
  Operation boom{"boom",Access::Read,"","object",{}};boom.handler=[](const Params&)->Json{throw std::runtime_error("boom");};s.operations.push_back(boom);out.str("");assert(run_cli_io(s,{"boom"},out,err)==1);assert(Json::parse(out.str())["error"]["code"]=="INTERNAL_ERROR");
}
