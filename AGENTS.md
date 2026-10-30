# Repository Guidelines

## Project Structure & Module Organization

`model/` contains reusable C++17 models. Each implemented component (`Common`, `ChiCommon`, `RN`, `HA`, `Memory`, and `SoC`) keeps public headers directly in `include/`, implementation files in `src/`, and its target definition in `CMakeLists.txt`. `top/` is the executable and integration layer: `top/src/main.cpp` builds `tocEslSim`, while `top/tests/` contains contract and subsystem tests. Put scenario inputs in `benchmark/`, design notes in `doc/`, and development utilities in the intentionally named `scrpits/` directory. Never commit generated content from `top/build/` or `top/run/`, or the local CHI specification PDF.

## Build, Test, and Development Commands

Use the dedicated WSL Debian 12 environment; configuration rejects compilers other than GCC 12.2.x.

```bash
cmake -S top -B top/build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build top/build/debug --parallel
ctest --test-dir top/build/debug --output-on-failure
./top/build/debug/tocEslSim
```

Repeat with `top/build/release` and `-DCMAKE_BUILD_TYPE=Release` before merging changes that affect model behavior. Run `bash scrpits/checkToolchain.sh` to validate the configured compiler and C++17 runtime.

## Coding Style & Naming Conventions

Use four-space indentation and follow existing C++ formatting. Warnings (`-Wall -Wextra -Wpedantic`) are errors. Use lowerCamelCase for variables, functions, targets, tests, scripts, and internal directories; use UpperCamelCase for types and their matching files, such as `BaseModule.h`. Macros use `UPPER_SNAKE_CASE`. All project symbols belong under the `toc` root namespace. Public headers stay directly under a component’s `include/` directory. Follow the established project convention of `using namespace std;` instead of repeated `std::` qualifiers.

Every model derives from `BaseModule`, implements `work()` and `xfer()`, owns its `CHIPort`, and communicates with other models only through CHI ports. SoC connects ports and advances the shared `EnvCfg` cycle.

## Testing Guidelines

Tests are standalone CTest executables named `*Test.cpp`; registered tests use lowerCamelCase names ending in `Contract`. Add focused tests under `top/tests/<feature>/` and register that directory in `top/CMakeLists.txt`. There is no percentage coverage target; cover cycle visibility, credit/backpressure behavior, invalid inputs, and returned data for changed protocols.

## Commit & Pull Request Guidelines

Use short imperative commit subjects, following the initial history style: `Initial ESL platform with RN memory read path`. Keep commits scoped to one behavior or interface. Pull requests should explain the trigger, resulting behavior, affected protocol flow, and Debug/Release validation. Link relevant issues and include logs or traces when timing or transaction ordering changes; screenshots are only useful for new visual tooling.
