# BlogAlone

BlogAlone is a small forum backend built with C++20, Drogon, SQLite, libsodium, cmark-gfm and spdlog.

## Build

On this Windows development machine, CMake is provided by Visual Studio:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --preset windows-vs
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset windows-vs-debug --target deps_smoke
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset windows-vs-debug --target blogalone
```

If CMake is on `PATH`, the same presets work with `cmake --preset windows-vs`.

