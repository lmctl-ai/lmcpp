#include <lmcpp/lmcpp.hpp>
#include <cassert>

int main() {
  lmcpp::CliSpec spec{"consumer", "0.1.0", {}};
  const auto schema = lmcpp::publish_schema(spec);
  assert(schema.at("schema") == "cli.schema.v1");
  assert(schema.at("tool") == "consumer");
}
