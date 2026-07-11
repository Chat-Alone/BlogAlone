# BlogAlone

BlogAlone是一个使用C++20、Drogon、SQLite、libsodium、cmark-gfm和spdlog构建的小型论坛后端。

## 构建

### Windows

```powershell
cmake --preset windows-vs
cmake --build --preset windows-vs-debug --target blogalone blogalone_unit_tests blogalone_integration_tests
ctest --preset windows-vs-debug --output-on-failure
```

### Linux

以下命令以Ubuntu为例：

```bash
sudo apt-get install build-essential cmake ninja-build git curl zip unzip tar pkg-config autoconf autoconf-archive automake libtool python3 perl nasm
cmake -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build-linux --parallel --target blogalone blogalone_unit_tests blogalone_integration_tests
ctest --test-dir build-linux --output-on-failure
```

## 运行

```bash
./blogalone --config config/config.windows.json   # 部署到Linux时改用config/config.linux.json
```

服务默认监听`127.0.0.1:8080`，访问`GET /api/healthz`即可确认启动状态。生产环境部署前，需要提前建好`config.linux.json`里约定的`/var/lib/blogalone/`、`/etc/blogalone/`、`/var/log/blogalone/`等目录。
