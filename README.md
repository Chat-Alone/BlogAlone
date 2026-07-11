# BlogAlone

BlogAlone是一个使用C++20、Drogon、SQLite、libsodium、cmark-gfm和spdlog构建的小型论坛。

## Windows

```powershell
cmake --preset windows-vs
cmake --build --preset windows-vs-debug --target blogalone blogalone_unit_tests blogalone_integration_tests
ctest --preset windows-vs-debug --output-on-failure
```

编译产物在`build-msvc\Debug`下，仓库根目录执行即可启动：

```powershell
build-msvc\Debug\blogalone.exe --config config\config.windows.json
```

## Linux

以下命令以Ubuntu为例：

```bash
sudo apt-get install build-essential cmake ninja-build git curl zip unzip tar pkg-config autoconf autoconf-archive automake libtool python3 perl nasm
cmake -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build-linux --parallel --target blogalone blogalone_unit_tests blogalone_integration_tests
ctest --test-dir build-linux --output-on-failure
```

首次配置需要编译全部依赖，耗时较长；后续增量编译只处理改动部分，速度快得多。

编译产物在`build-linux`下。本地验证同样在仓库根目录执行，配置用`config.windows.json`即可，它的`migrations`、`web`、`uploads`都是相对仓库根目录的路径：

```bash
./build-linux/blogalone --config config/config.windows.json
```

`config.linux.json`是面向生产部署的配置，`filename`、`web_root`、`uploads_root`写成了`/var/lib/blogalone/`和`/opt/blogalone/`下的绝对路径，只有按部署文档提前建好这些目录才能启动，本地开发不要直接用它。

## 服务状态

服务默认监听`127.0.0.1:8080`，访问`GET /api/healthz`即可确认启动状态。
