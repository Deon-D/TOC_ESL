#!/usr/bin/env bash
set -euo pipefail
projectRoot="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
buildDir="$projectRoot/top/build/toolchainSmoke"
cmake -S "$projectRoot/top/tests/toolchainSmoke" -B "$buildDir" \
    -G Ninja -DCMAKE_CXX_COMPILER=g++-12 -DCMAKE_BUILD_TYPE=Debug
cmake --build "$buildDir" --parallel
ctest --test-dir "$buildDir" --output-on-failure --verbose
