# BlogAlone

BlogAlone是一个使用C++20、Drogon和SQLite构建的小型中文论坛。

## Windows构建

构建环境需要Visual Studio2022、MSVC v143、Windows SDK、CMake3.25或更高版本。依赖通过vcpkg清单安装。CMake会查找`VCPKG_ROOT`，也会检查Visual Studio自带的vcpkg。

```powershell
cmake --preset windows-vs
cmake --build --preset windows-vs-debug --target blogalone blogalone_unit_tests blogalone_integration_tests
ctest --preset windows-vs-debug --output-on-failure
```

编译产物位于`build-msvc\Debug`。

## Linux构建

Ubuntu环境安装编译工具：

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build git curl zip unzip tar pkg-config autoconf autoconf-archive automake libtool python3 perl nasm
```

安装vcpkg并设置环境变量：

```bash
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_ROOT="$HOME/vcpkg"
```

配置、构建并运行测试：

```bash
cmake -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-linux --parallel --target blogalone blogalone_unit_tests blogalone_integration_tests
ctest --test-dir build-linux --output-on-failure
```

首次构建会编译全部依赖，耗时较长。编译产物位于`build-linux`。

## 本地运行

在仓库根目录启动服务。Windows使用：

```powershell
build-msvc\Debug\blogalone.exe --config config\config.development.json
```

Linux使用：

```bash
./build-linux/blogalone --config config/config.development.json
```

本地配置将数据库写入`blogalone.dev.db`，页面和上传目录分别使用仓库中的`web`与`uploads`。服务默认监听`127.0.0.1:8080`，健康检查地址为`http://127.0.0.1:8080/api/healthz`。

## 配置文件

本地开发使用[config/config.development.json](config/config.development.json)。请从仓库根目录启动服务，数据库、迁移、页面和上传目录会按当前工作目录解析。

Linux生产部署以[config/config.production.json](config/config.production.json)为模板。安装时将其复制到`/etc/blogalone/config.json`。模板使用下列路径：

- 数据库：`/var/lib/blogalone/blogalone.db`
- 迁移：`/opt/blogalone/migrations`
- 页面：`/opt/blogalone/web`
- 上传：`/var/lib/blogalone/uploads`

### HTTP与进程

| 字段 | 含义 |
| --- | --- |
| `listeners[].address` | 监听地址。通过本机nginx代理时使用`127.0.0.1`。 |
| `listeners[].port` | HTTP监听端口，默认值为`8080`。 |
| `listeners[].https` | 是否由Drogon直接处理HTTPS。使用nginx终止TLS时应设为`false`。 |
| `app.threads_num` | Drogon工作线程数。可按服务器CPU和负载调整。 |
| `app.run_as_daemon` | 由systemd管理时应设为`false`。 |
| `client_max_body_size` | HTTP请求体上限，单位为字节。该值应大于图片上传上限。 |

### SQLite与迁移

```json
{
  "db_clients": [
    {
      "name": "default",
      "rdbms": "sqlite3",
      "filename": "/var/lib/blogalone/blogalone.db",
      "is_fast": false,
      "number_of_connections": 1
    }
  ]
}
```

`default`客户端必须使用SQLite，`number_of_connections`必须保持为`1`。迁移插件中的`database_path`必须与`db_clients[].filename`完全一致，`db_client`必须为`default`。

```json
{
  "name": "blogalone::plugins::DatabaseMigrationPlugin",
  "dependencies": [],
  "config": {
    "database_path": "/var/lib/blogalone/blogalone.db",
    "migrations_dir": "/opt/blogalone/migrations",
    "db_client": "default"
  }
}
```

相对路径以启动进程的工作目录为基准，不以配置文件所在目录为基准。开发配置需要从仓库根目录使用。生产模板中的运行路径均为绝对路径。

### 应用参数

应用参数位于`custom_config`。

| 字段 | 含义 |
| --- | --- |
| `trusted_proxies` | 可信反向代理IP列表。只有来自这些地址的请求才会采用`X-Forwarded-For`。 |
| `web_root` | `web`目录路径，生产环境通常为`/opt/blogalone/web`。 |
| `uploads_root` | 上传文件目录，生产环境通常为`/var/lib/blogalone/uploads`。 |
| `session_ttl_seconds` | 登录会话有效时间，单位为秒。 |
| `upload_max_file_size` | 单个上传文件的最大字节数。 |
| `upload_max_daily_uploads` | 每名用户每天允许上传的文件数量。 |
| `upload_max_dimension` | 图片宽度或高度的最大像素数，不得超过`5792`。 |
| `rate_limit_*_max_requests` | 对应操作在一个窗口内允许的请求次数。 |
| `rate_limit_*_window_seconds` | 对应限流窗口的秒数。 |
| `orphan_upload_retention_seconds` | 未绑定上传文件的保留时间。 |
| `upload_cleanup_interval_seconds` | 孤立上传清理任务的运行间隔。 |
| `session_cleanup_interval_seconds` | 过期会话清理任务的运行间隔。 |
| `password_opslimit` | Argon2id计算强度。生产配置应根据服务器性能测试确定。 |
| `password_memlimit` | 单次密码哈希允许使用的内存字节数。 |

主机目录或反向代理地址与模板不一致时，修改对应路径和`trusted_proxies`。`web_root`、`uploads_root`、`migrations_dir`及数据库父目录必须在服务启动前存在。

修改配置后运行检查：

```bash
/opt/blogalone/blogalone --check-config --config /etc/blogalone/config.json
```

Windows本地检查使用：

```powershell
build-msvc\Debug\blogalone.exe --check-config --config config\config.development.json
```

## 管理员命令

管理员密码只能从文件读取。密码文件应只允许管理员账号访问，命令执行成功后应立即删除。

Windows开发环境：

```powershell
build-msvc\Debug\blogalone.exe admin create `
  --config config\config.development.json `
  --username site_admin `
  --password-file C:\secure\admin-password.txt
```

Linux生产环境：

```bash
sudo sh -c 'umask 077; printf "%s\n" "替换为强密码" > /root/blogalone-admin-password'
sudo /opt/blogalone/blogalone admin create \
  --username site_admin \
  --password-file /root/blogalone-admin-password
sudo rm /root/blogalone-admin-password
```

Linux命令未指定`--config`时默认读取`/etc/blogalone/config.json`。数据库已有管理员时会拒绝创建。确需新增本机管理员可传入`--force`，操作记录写入`audit_log`。

## Linux部署

安装运行和运维工具：

```bash
sudo apt-get update
sudo apt-get install nginx sqlite3 curl python3 tar coreutils util-linux
```

生产目录约定如下：

```text
/opt/blogalone/                 二进制、web和migrations
/etc/blogalone/                 配置文件
/var/lib/blogalone/blogalone.db SQLite数据库
/var/lib/blogalone/uploads/     上传文件
/backup/blogalone/              备份文件
```

创建服务账号和目录：

```bash
sudo useradd --system --home /var/lib/blogalone --shell /usr/sbin/nologin blogalone
sudo install -d -o root -g root -m 0755 /opt/blogalone /etc/blogalone
sudo install -d -o blogalone -g blogalone -m 0750 /var/lib/blogalone /var/lib/blogalone/uploads
sudo install -d -o root -g root -m 0700 /backup/blogalone
```

安装二进制、页面、迁移、运维脚本和配置文件：

```bash
sudo install -m 0755 build-linux/blogalone /opt/blogalone/blogalone
sudo cp -a web migrations /opt/blogalone/
sudo install -m 0755 deploy/backup.sh deploy/update.sh deploy/restore-drill.sh /opt/blogalone/
sudo install -m 0640 -o root -g blogalone config/config.production.json /etc/blogalone/config.json
```

主机目录或代理地址与模板不一致时，修改`/etc/blogalone/config.json`。运行配置检查并创建管理员后，安装systemd服务：

```bash
sudo install -m 0644 deploy/blogalone.service /etc/systemd/system/blogalone.service
sudo systemctl daemon-reload
sudo systemctl enable --now blogalone.service
```

复制`deploy/nginx.conf`前必须替换域名和证书路径。安装后检查配置并重载nginx：

```bash
sudo install -m 0644 deploy/nginx.conf /etc/nginx/sites-available/blogalone.conf
sudo ln -s /etc/nginx/sites-available/blogalone.conf /etc/nginx/sites-enabled/blogalone.conf
sudo nginx -t
sudo systemctl reload nginx
```

[部署手册](deploy/README.md)提供完整的主机准备和运维说明，内容包括：

- nginx域名、TLS和反向代理配置
- systemd服务安装与启动
- sqlite3在线备份和保留策略
- 临时目录恢复演练
- 带健康检查和自动回滚的二进制更新

部署完成后检查服务：

```bash
systemctl status blogalone.service
curl --fail http://127.0.0.1:8080/api/healthz
sudo nginx -t
```
