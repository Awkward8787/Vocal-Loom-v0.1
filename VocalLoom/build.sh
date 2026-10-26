#!/bin/zsh
set -euo pipefail
project_dir="${0:A:h}"
build_dir="${VOCAL_LOOM_BUILD_DIR:-${project_dir}/work/build}"
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 "$@"
cmake --build "$build_dir" -j 6
ctest --test-dir "$build_dir" --output-on-failure
print "Builds: ${build_dir}/VocalLoom_artefacts/Release"
