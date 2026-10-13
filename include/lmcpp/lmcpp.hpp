#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace lmcpp {
using Json = nlohmann::ordered_json;
using Params = Json;
enum class Type { String, Number, Integer, Boolean, Object, Array, Json };
enum class Access { Read, Write };
enum class ErrorCode { InvalidRequest, ValidationError, ParseError, NotFound, Conflict, Forbidden, ReadOnlyViolation, Timeout, ServiceUnavailable, MissingConfirmation, UnsupportedProductVersion, InternalError, CommandFailed };
struct Param { std::string name; Type type; bool required=true; Json default_value=nullptr; std::vector<Json> enum_values; std::string description; std::string occurs="1"; bool positional=false, cli_only=false, doc_only=false; std::vector<std::string> aliases; Type item_type=Type::String; };
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
