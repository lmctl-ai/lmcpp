#!/bin/sh
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
if [ -d "$repo_root/../lmapi/conformance" ]; then
  lmapi_root=$(CDPATH= cd -- "$repo_root/../lmapi" && pwd)
  exec docker run --rm -u "$(id -u):$(id -g)" -v "$repo_root:$repo_root" -v "$lmapi_root:$lmapi_root" -w "$repo_root" lmcpp-dev "$@"
fi
exec docker run --rm -u "$(id -u):$(id -g)" -v "$repo_root:$repo_root" -w "$repo_root" lmcpp-dev "$@"
