# BlogAlone部署手册

本目录提供nginx反向代理、systemd服务、SQLite在线备份、更新回滚和恢复演练文件。目标系统为使用systemd的Linux发行版。

## 主机准备

安装运行工具：

```bash
sudo apt-get install nginx sqlite3 curl python3 tar coreutils util-linux
```

创建专用账号和目录：

```bash
sudo useradd --system --home /var/lib/blogalone --shell /usr/sbin/nologin blogalone
sudo install -d -o root -g root -m 0755 /opt/blogalone /etc/blogalone
sudo install -d -o blogalone -g blogalone -m 0750 /var/lib/blogalone /var/lib/blogalone/uploads
sudo install -d -o root -g root -m 0700 /backup/blogalone
```

把编译后的`blogalone`、`web`和`migrations`安装到`/opt/blogalone`，把生产配置安装到`/etc/blogalone`：

```bash
sudo install -m 0755 build-linux/blogalone /opt/blogalone/blogalone
sudo cp -a web migrations /opt/blogalone/
sudo install -m 0640 -o root -g blogalone config/config.production.json /etc/blogalone/config.json
sudo install -m 0755 deploy/backup.sh deploy/update.sh deploy/restore-drill.sh /opt/blogalone/
```

配置检查会验证JSON、自定义参数、单连接SQLite约束、迁移配置，以及数据库父目录、上传目录、页面目录和迁移目录：

```bash
/opt/blogalone/blogalone --check-config --config /etc/blogalone/config.json
```

## 创建初始管理员

管理员命令从权限受限的文件读取密码，不接受命令行明文密码。用户名规则与注册接口相同，密码长度为8至128字节。

```bash
sudo sh -c 'umask 077; printf "%s\n" "替换为强密码" > /root/blogalone-admin-password'
sudo /opt/blogalone/blogalone admin create \
  --username site_admin \
  --password-file /root/blogalone-admin-password
sudo rm /root/blogalone-admin-password
```

命令默认使用`/etc/blogalone/config.json`。数据库内已有管理员时会拒绝创建。确需新增本机管理员可传`--force`，该操作会写入`admin.bootstrap_force`审计记录：

```bash
sudo /opt/blogalone/blogalone admin create \
  --username recovery_admin \
  --password-file /root/blogalone-admin-password \
  --force
```

## systemd与nginx

安装并启动服务：

```bash
sudo install -m 0644 deploy/blogalone.service /etc/systemd/system/blogalone.service
sudo systemctl daemon-reload
sudo systemctl enable --now blogalone.service
curl --fail http://127.0.0.1:8080/api/healthz
```

复制`deploy/nginx.conf`前替换域名和证书路径。配置通过检查后再重载nginx：

```bash
sudo install -m 0644 deploy/nginx.conf /etc/nginx/sites-available/blogalone.conf
sudo ln -s /etc/nginx/sites-available/blogalone.conf /etc/nginx/sites-enabled/blogalone.conf
sudo nginx -t
sudo systemctl reload nginx
```

## 日志保留

`blogalone.service`把标准输出和标准错误交给journal(`StandardOutput=journal`、`StandardError=journal`),spdlog本身不写文件,因此保留期由journald而不是logrotate控制。安装保留期配置：

```bash
sudo install -m 0644 deploy/journald-blogalone.conf /etc/systemd/journald.conf.d/blogalone.conf
sudo systemctl restart systemd-journald
```

该配置把`MaxRetentionSec`设为30天,与备份保留策略保持一致。`journald.conf.d`是主机级设置,会影响整机日志保留,不止blogalone一个服务。

## 备份与保留

`backup.sh`用sqlite3的`.backup`创建一致性数据库副本，执行完整性检查，再归档上传目录并生成SHA-256清单。脚本保留7天日备份和28天周备份，周备份在UTC星期日生成。

```bash
sudo /opt/blogalone/backup.sh
```

输出内容是本次备份前缀，恢复演练直接使用该值。可通过环境变量修改路径：

- `BLOGALONE_DATABASE_PATH`
- `BLOGALONE_UPLOADS_PATH`
- `BLOGALONE_BACKUP_ROOT`

用提供的systemd timer每日执行，失败必须进入主机告警：

```bash
sudo install -m 0644 deploy/blogalone-backup.service deploy/blogalone-backup.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now blogalone-backup.timer
systemctl list-timers blogalone-backup.timer
```

计时器默认在每天UTC03:00附近触发(带15分钟随机延迟，避免与其他主机的定时任务撞点)，`Persistent=true`保证关机错过触发点后开机会补跑一次。失败的运行可通过`journalctl -u blogalone-backup.service`查看。

## 恢复演练

恢复脚本校验备份清单和SQLite完整性，在临时目录恢复数据库与上传文件，生成隔离配置并启动真实BlogAlone进程。脚本会请求健康接口、板块接口、首个主题详情和首张已记录图片，退出时清理临时文件。

```bash
sudo /opt/blogalone/restore-drill.sh \
  /backup/blogalone/daily-20260712-120000 \
  /opt/blogalone/blogalone \
  /etc/blogalone/config.json
```

演练默认监听`127.0.0.1:18081`，可用`BLOGALONE_RESTORE_PORT`改端口。至少在首次上线、迁移变更和恢复流程修改后执行一次。

## 带回滚更新

`update.sh`接收一个已经上传到本机的发布目录。目录内必须包含`blogalone`、`web`和`migrations`。流程包含新版配置检查、强制备份、停服务、整体替换、启动和健康检查。任一步失败都会恢复旧二进制、页面、迁移、数据库和上传目录，再重新启动服务。

```bash
sudo /opt/blogalone/update.sh /tmp/blogalone-release /etc/blogalone/config.json
```

发布目录结构如下：

```text
/tmp/blogalone-release/
├── blogalone
├── web/
└── migrations/
```

配置文件仍由管理员单独维护。配置变更应在执行脚本前写入`/etc/blogalone/config.json`，脚本会在停服务前用新二进制检查该配置。不要提前覆盖`/opt/blogalone`中的运行文件，脚本需要保留当前版本用于自动回滚。
