# Changelog

All notable changes to lmcpp are documented here.

## [0.1.0] - Unreleased

### Added

- C++17 server SDK for `cli.schema.v1`, flags and JSON validation, policy floors, result files, and error envelopes.
- Toy native tool, lmapi server conformance tests, installable CMake package, and consumer smoke test.
- POSIX `run_child` helper for isolated argv execution with environment replacement, timeout/process-group kill, and bounded output capture.

### Fixed

- Route runtime calls through the tested argv parser and preserve raw string values.
- Bound JSON depth and file reads; safely serialize invalid UTF-8 failures.
- Publish protocol-compatible parameter types and metadata, and derive requiredness from `occurs`.
- Add runtime argv fixture coverage and minimal server discovery commands.
- Enforce the JSON depth limit for structured argv values, preserve negative numeric tokens, and recognize document envelopes only by their complete key set.
- Report duplicate document flags accurately and make the Docker development script checkout-location independent.
