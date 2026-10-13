#!/bin/sh
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
exec docker run --rm -u "$(id -u):$(id -g)" -v "$repo_root:$repo_root" -w "$repo_root" lmcpp-dev "$@"
