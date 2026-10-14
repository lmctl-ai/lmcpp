#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <chrono>

namespace lmcpp {
using Json = nlohmann::ordered_json;
using Params = Json;
enum class Type { String, Number, Integer, Boolean, Object, Array, Json };
enum class Access { Read, Write };
enum class ErrorCode { InvalidRequest, ValidationError, ParseError, NotFound, Conflict, Forbidden, ReadOnlyViolation, Timeout, ServiceUnavailable, MissingConfirmation, UnsupportedProductVersion, InternalError, CommandFailed };
struct Param {
  std::string name;
  Type type=Type::String;
  Json default_value=nullptr;
  std::vector<Json> enum_values;
  std::string description;
  std::string occurs="1";
  bool positional=false, cli_only=false, doc_only=false;
  std::vector<std::string> aliases;
  Type item_type=Type::String;
  Param()=default;
  Param(std::string n, Type t, bool required=true, Json def=nullptr,
        std::vector<Json> values={}, std::string desc={}, std::string cardinality="",
        bool pos=false, bool cli=false, bool doc=false,
        std::vector<std::string> names={}, Type item=Type::String)
      : name(std::move(n)), type(t), default_value(std::move(def)),
        enum_values(std::move(values)), description(std::move(desc)),
        occurs(cardinality.empty() ? (required ? "1" : "?") : std::move(cardinality)),
        positional(pos), cli_only(cli), doc_only(doc), aliases(std::move(names)), item_type(item) {}
};
struct Context;
struct Operation { std::string name; Access access=Access::Read; std::string description; std::string returns="object"; std::vector<Param> params; std::function<Json(const Params&)> handler; std::function<Json(const Params&,Context&)> context_handler; bool dangerous=false; };
struct CliSpec { std::string tool, version; std::vector<Operation> operations; };
struct Error { ErrorCode code; std::string message; Json data=Json::object(); };
class Context {
 public:
  Context(); ~Context(); Context(const Context&)=delete; Context& operator=(const Context&)=delete;
  const std::string& temp_file_path();
 private: std::string path_;
};
struct ChildResult { int exit_code=-1; bool timed_out=false; std::string out, err; };
// Executes argv directly (never through a shell), with the supplied environment,
// bounded output capture, and process-group termination on timeout.
ChildResult run_child(const std::vector<std::string>& argv,
                      const std::map<std::string,std::string>& env,
                      std::chrono::milliseconds timeout,
                      std::size_t max_capture=1024*1024);
Json read_result_file(const std::string& path, const std::string& format);
Json publish_schema(const CliSpec& spec);
Json validate_params(const Operation& op, const Json& raw, const std::string& form="json");
Json parse_published_schema(const Json& document);
Json parse_argv(const Operation& op, const std::vector<std::string>& argv, const std::vector<Param>& globals={});
struct Resolved { std::string op; Params params; };
Resolved resolve_input(const CliSpec& spec, const std::vector<std::string>& args);
int run_cli_io(const CliSpec& spec, const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
int run_cli(const CliSpec& spec, int argc, char** argv);
}
