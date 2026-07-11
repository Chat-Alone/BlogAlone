# BlogAlone

BlogAlone is a small forum backend built with C++20, Drogon, SQLite, libsodium, cmark-gfm and spdlog.

## Build

On this Windows development machine, CMake is provided by Visual Studio:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --preset windows-vs
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset windows-vs-debug --target deps_smoke
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset windows-vs-debug --target blogalone
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset windows-vs-debug --target blogalone_unit_tests blogalone_integration_tests
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --preset windows-vs-debug --output-on-failure
```

If CMake is on `PATH`, the same presets work with `cmake --preset windows-vs`.
Use `ctest --preset windows-vs-debug -L unit` or `-L integration` to run one test layer.

On Ubuntu, install the native build tools and bootstrap vcpkg before configuring:

```bash
sudo apt-get install build-essential cmake ninja-build git curl zip unzip tar pkg-config autoconf autoconf-archive automake libtool python3 perl nasm
cmake -S /opt/BlogAlone -B /opt/BlogAlone/build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build /opt/BlogAlone/build-linux --parallel --target deps_smoke blogalone blogalone_unit_tests blogalone_integration_tests
/opt/BlogAlone/build-linux/deps_smoke
ctest --test-dir /opt/BlogAlone/build-linux --output-on-failure
```
