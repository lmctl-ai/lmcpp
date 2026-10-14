# Changelog

All notable changes to lmcpp are documented here.

## [0.1.0] - Unreleased

### Added

- C++17 server SDK for `cli.schema.v1`, flags and JSON validation, policy floors, result files, and error envelopes.
- Toy native tool, lmapi server conformance tests, installable CMake package, and consumer smoke test.
- Harden POSIX `run_child`: `/dev/null` stdin, closed inherited descriptors, reset child signals, Linux parent-death handling, explicit exec/status errors, signal and truncation fields, and timeout return despite escaped descendants.
- Match protocol/lmts array flag behavior: scalar item flags, repeated values, equals syntax, and coercion by published item type; add a local conformance proposal.
- Allow numeric params to opt into explicit `null`, `NA`, `NaN`, `Inf`, and `-Inf` values without changing their published numeric type.

### Fixed

- Route runtime calls through the tested argv parser and preserve raw string values.
- Bound JSON depth and file reads; safely serialize invalid UTF-8 failures.
- Publish protocol-compatible parameter types and metadata, and derive requiredness from `occurs`.
- Add runtime argv fixture coverage and minimal server discovery commands.
- Enforce the JSON depth limit for structured argv values, preserve negative numeric tokens, and recognize document envelopes only by their complete key set.
- Report duplicate document flags accurately and make the Docker development script checkout-location independent.
