# BlogAlone技术方案

BlogAlone是一个小型贴吧。访客可以查看板块和帖子；登录用户可以发帖、回帖、回复楼中楼、编辑和删除自己的内容；管理员负责板块维护、置顶、加精、删帖、恢复内容、封禁和管理员授权。

前端保持简单，使用原生HTML5和JavaScript。后端提供HTML页面、静态资源、上传文件访问和JSON接口，nginx只负责TLS、域名、安全响应头和反向代理。业务逻辑集中在服务层，控制器只做参数校验、鉴权结果读取和响应转换。

---

## 技术选型

| 模块 | 方案 | 说明 |
| --- | --- | --- |
| HTTP框架 | Drogon | 使用C++20协程处理请求 |
| 数据库 | SQLite | 开启WAL模式，通过DrogonORM和DbClient访问 |
| 登录状态 | SQLite会话表+Cookie | Cookie设置HttpOnly、Secure、SameSite=Lax和Path=/ |
| CSRF防护 | 会话级CSRF令牌 | 登录后返回CSRF令牌，所有写接口要求`X-CSRF-Token` |
| 密码存储 | libsodium的Argon2id | 密码参数放入配置，登录耗时目标为200ms到800ms |
| Markdown | cmark-gfm | 后端渲染HTML，禁用原始HTML，渲染结果缓存到表内 |
| HTML安全 | cmark AST白名单清理 | 渲染前处理链接、图片和原始HTML，禁止正则清理HTML |
| 日志 | spdlog | 异步写入，按天切分文件，记录request_id |
| 测试 | GoogleTest+DrogonHttpClient | 单元测试覆盖服务层，集成测试覆盖HTTP链路和Cookie |
| 构建 | CMake3.25+ | 依赖锁定版本，先通过依赖冒烟构建再写业务代码 |
| 页面提供 | Drogon | 后端直接返回HTML页面和前端静态资源 |
| 部署 | nginx+systemd | nginx处理TLS、域名、安全响应头和反向代理，systemd管理后端进程 |

---

## 依赖管理

依赖统一写进`cmake/deps.cmake`，每个依赖都锁定到明确tag或commit。新机器拉取代码后，执行`cmake -B build`即可开始构建，不要求提前安装系统级开发包。若OpenSSL或Drogon在Windows上通过FetchContent反复失败，再切换到`vcpkg.json`清单模式，避免在业务代码完成后才处理依赖问题。

纳入构建的第三方库：

- Drogon
- OpenSSL
- zlib
- jsoncpp
- sqlite3
- spdlog
- libsodium
- cmark-gfm
- GoogleTest

Drogon会用到OpenSSL、zlib、jsoncpp和sqlite3。`FetchContent_MakeAvailable`的顺序固定为底层库、Drogon、业务辅助库、测试库，避免Drogon配置阶段找不到依赖目标。

Windows开发机需要提前安装：

- VisualStudio2022，包含MSVCv143和WindowsSDK
- StrawberryPerl，供OpenSSL构建脚本使用
- NASM，供OpenSSL汇编优化使用
- CMake3.25+

Linux部署机需要提前安装：

- GCC11+或Clang14+，需要支持C++20协程
- CMake3.25+
- Perl
- NASM

首次配置会编译所有依赖。Windows上可能需要30到60分钟；后续只编译变动部分，耗时会明显缩短。

为降低依赖风险，仓库保留一个`deps_smoke`目标，只链接Drogon、SQLite、libsodium、cmark-gfm和spdlog，功能代码开写前必须先让该目标在Windows和Linux上通过。

---

## 数据库配置

每个SQLite连接打开后都执行下列配置：

```sql
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA busy_timeout = 5000;
```

SQLite保持单写者模型。Drogon连接池不追求大量写连接，初始配置为读连接4个、写连接1个。所有写操作由服务层显式开启事务，事务内只做数据库读写，不执行Markdown渲染、图片解码、文件复制等耗时操作。

所有时间字段使用UTC Unix秒。服务端负责生成时间，客户端传来的时间只用于展示偏好，不能参与权限和排序判断。

迁移通过启动插件执行。插件读取`schema_migrations`，按版本顺序运行SQL，记录版本号、校验值和执行时间。每个迁移脚本用`BEGIN IMMEDIATE`包裹，失败时回滚并阻止服务继续启动。

---

## 数据表

基础迁移以明确约束为准，不能只依赖服务层校验。

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    username TEXT NOT NULL UNIQUE COLLATE NOCASE,
    email TEXT UNIQUE COLLATE NOCASE,
    pwd_hash TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'user' CHECK (role IN ('user', 'admin')),
    banned_until INTEGER,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    avatar_url TEXT
);

CREATE TABLE sessions (
    token_hash TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    csrf_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    revoked_at INTEGER,
    admin_confirmed_at INTEGER,
    ip TEXT NOT NULL,
    user_agent TEXT NOT NULL
);

CREATE TABLE forums (
    id INTEGER PRIMARY KEY,
    slug TEXT NOT NULL UNIQUE COLLATE NOCASE,
    name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE threads (
    id INTEGER PRIMARY KEY,
    forum_id INTEGER NOT NULL REFERENCES forums(id) ON DELETE RESTRICT,
    author_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    title TEXT NOT NULL,
    body_md TEXT NOT NULL,
    body_html TEXT NOT NULL,
    is_pinned INTEGER NOT NULL DEFAULT 0 CHECK (is_pinned IN (0, 1)),
    is_featured INTEGER NOT NULL DEFAULT 0 CHECK (is_featured IN (0, 1)),
    is_deleted INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
    deleted_by INTEGER REFERENCES users(id) ON DELETE SET NULL,
    deleted_at INTEGER,
    reply_count INTEGER NOT NULL DEFAULT 0 CHECK (reply_count >= 0),
    last_reply_at INTEGER,
    last_reply_user_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE posts (
    id INTEGER PRIMARY KEY,
    thread_id INTEGER NOT NULL REFERENCES threads(id) ON DELETE CASCADE,
    author_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    floor_no INTEGER NOT NULL CHECK (floor_no > 0),
    body_md TEXT NOT NULL,
    body_html TEXT NOT NULL,
    is_deleted INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
    deleted_by INTEGER REFERENCES users(id) ON DELETE SET NULL,
    deleted_at INTEGER,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    UNIQUE (thread_id, floor_no)
);

CREATE TABLE sub_posts (
    id INTEGER PRIMARY KEY,
    post_id INTEGER NOT NULL REFERENCES posts(id) ON DELETE CASCADE,
    author_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    body_md TEXT NOT NULL,
    body_html TEXT NOT NULL,
    reply_to_user_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
    is_deleted INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
    deleted_by INTEGER REFERENCES users(id) ON DELETE SET NULL,
    deleted_at INTEGER,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE uploads (
    id INTEGER PRIMARY KEY,
    sha256 TEXT NOT NULL UNIQUE,
    path TEXT NOT NULL UNIQUE,
    mime TEXT NOT NULL,
    size INTEGER NOT NULL CHECK (size > 0),
    width INTEGER,
    height INTEGER,
    created_at INTEGER NOT NULL
);

CREATE TABLE upload_refs (
    id INTEGER PRIMARY KEY,
    owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    upload_id INTEGER NOT NULL REFERENCES uploads(id) ON DELETE CASCADE,
    created_at INTEGER NOT NULL,
    attached_at INTEGER,
    UNIQUE (owner_id, upload_id)
);

CREATE TABLE audit_log (
    id INTEGER PRIMARY KEY,
    admin_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
    action TEXT NOT NULL,
    target_type TEXT NOT NULL,
    target_id INTEGER NOT NULL,
    detail TEXT NOT NULL,
    created_at INTEGER NOT NULL
);

CREATE TABLE schema_migrations (
    version INTEGER PRIMARY KEY,
    checksum TEXT NOT NULL,
    applied_at INTEGER NOT NULL
);
```

关键索引：

```sql
CREATE INDEX idx_threads_forum_last_reply ON threads(forum_id, is_deleted, is_pinned DESC, last_reply_at DESC);
CREATE INDEX idx_posts_thread_floor ON posts(thread_id, floor_no);
CREATE INDEX idx_sub_posts_post_created ON sub_posts(post_id, created_at);
CREATE INDEX idx_sessions_user ON sessions(user_id);
CREATE INDEX idx_sessions_expires ON sessions(expires_at);
CREATE INDEX idx_upload_refs_owner ON upload_refs(owner_id, created_at);
CREATE INDEX idx_upload_refs_upload ON upload_refs(upload_id);
CREATE INDEX idx_upload_refs_attached ON upload_refs(attached_at);
```

评论采用贴吧常见的两层结构：帖子下面有楼层，楼层下面有楼中楼回复。

---

## 写入事务规则

发帖事务包含插入`threads`、设置`last_reply_at`为发帖时间。Markdown渲染和HTML清理在进入事务前完成，事务只保存已处理文本。管理员后台操作才写入`audit_log`。

回帖事务必须使用`BEGIN IMMEDIATE`。服务层在同一事务内读取当前最大`floor_no`、插入`posts`、更新`threads.reply_count`、`threads.last_reply_at`和`threads.last_reply_user_id`。`UNIQUE(thread_id,floor_no)`是并发兜底，撞号时重试一次，仍失败则返回`409 conflict`。

楼中楼回复事务插入`sub_posts`，更新主帖`last_reply_at`和`last_reply_user_id`。`reply_count`只统计楼层回复，不统计楼中楼，避免列表页数字含义混乱。

编辑内容时重新生成`body_html`，只更新对应记录的`body_md`、`body_html`和`updated_at`。普通用户只能编辑自己的未删除内容；管理员编辑他人内容必须写入`audit_log`。

删除采用软删除。普通用户删除自己的内容时设置`is_deleted=1`和`deleted_at`；管理员删除时还要设置`deleted_by`并写入`audit_log`。恢复内容只开放给管理员。

管理员授权变更使用`BEGIN IMMEDIATE`。提升用户为管理员时，当前管理员必须已通过二次密码确认，并写入`audit_log`。移除管理员权限时，禁止移除自己，禁止移除系统中仅剩的管理员，目标用户降级为`user`后撤销该用户所有仍有效的会话。该操作只改变权限，不删除用户账号和历史内容。

---

## 页面和资源路由

后端直接提供HTML入口页、前端静态资源和上传文件访问。HTML入口页只作为页面外壳，不在服务端拼接用户内容；帖子正文和回复仍通过JSON接口加载，Markdown渲染后的安全HTML由接口返回。

```http
GET /                         -> web/pages/index.html
GET /threads/:id              -> web/pages/thread.html
GET /static/*                 -> web/static/*
GET /uploads/YYYY/MM/...      -> 上传文件读取
```

---

## API路由

公开接口：

```http
GET  /api/forums
GET  /api/forums/:slug/threads?page&page_size
GET  /api/threads/:id?page&page_size
POST /api/auth/register
POST /api/auth/login
GET  /api/healthz
```

登录后可用：

```http
GET    /api/me
PATCH  /api/me
POST   /api/auth/logout
POST   /api/threads
PATCH  /api/threads/:id
DELETE /api/threads/:id
POST   /api/threads/:id/posts
PATCH  /api/posts/:id
DELETE /api/posts/:id
POST   /api/posts/:id/sub_posts
PATCH  /api/sub_posts/:id
DELETE /api/sub_posts/:id
POST   /api/uploads
```

管理员接口：

```http
POST   /api/admin/forums
PATCH  /api/admin/forums/:id
DELETE /api/admin/forums/:id
GET    /api/admin/users?role&page&page_size
POST   /api/admin/reauth
PATCH  /api/admin/threads/:id/pin
PATCH  /api/admin/threads/:id/feature
PATCH  /api/admin/threads/:id/delete
PATCH  /api/admin/posts/:id/delete
PATCH  /api/admin/sub_posts/:id/delete
PATCH  /api/admin/users/:id/role
PATCH  /api/admin/users/:id/ban
DELETE /api/admin/sessions/:token_hash
GET    /api/admin/audit_logs?page&page_size
```

`PATCH /api/admin/users/:id/role`的请求体只接受`{"role":"admin"}`和`{"role":"user"}`。角色变更成功后返回目标用户的最新资料。管理员列表通过`GET /api/admin/users?role=admin`获取，普通用户检索通过`role=user`或省略`role`完成。

分页规则固定为`page>=1`，`page_size`默认20，最大50。超出范围返回`400 invalid_argument`。所有列表接口返回`items`、`page`、`page_size`和`total`，MVP阶段先使用普通分页，数据量变大后再替换为游标分页。

错误响应统一格式：

```json
{
  "error": {
    "code": "invalid_argument",
    "message": "page_size out of range",
    "request_id": "req_..."
  }
}
```

常用错误码包括`invalid_argument`、`unauthenticated`、`forbidden`、`admin_reauth_required`、`not_found`、`conflict`、`rate_limited`和`internal_error`。前端只依赖`code`做分支，不解析`message`。

输入限制固定在服务端：

```text
username: 3到32个字符，只允许中文、字母、数字和下划线
email: 最大254个字符，入库前转小写
password: 8到128个字符
forum.slug: 2到32个字符，只允许小写字母、数字和短横线
thread.title: 1到80个字符
thread.body_md: 1到20000个字符
post.body_md: 1到20000个字符
sub_post.body_md: 1到2000个字符
```

所有文本字段入库前裁剪首尾空白。裁剪后为空的输入直接返回`400 invalid_argument`。

请求进入控制器前经过五层过滤：

```text
RequestIdFilter -> RealIpFilter -> SessionFilter -> CsrfFilter -> RoleFilter
```

`RequestIdFilter`生成或继承`X-Request-Id`。`RealIpFilter`识别真实客户端IP。`SessionFilter`读取Cookie，验证会话未过期、未撤销、用户未被封禁，并把`userId`和`role`放进请求属性。`CsrfFilter`拦截POST、PATCH、PUT和DELETE，但放行注册和登录。`RoleFilter`只挂在管理员路由上。

---

## 安全策略

会话token由32字节安全随机数生成，Cookie中保存明文token，数据库只保存`SHA-256(token)`。CSRF令牌也使用32字节安全随机数生成，登录响应体返回明文，数据库保存哈希值，并设置可被前端读取的`ba_csrf`Cookie，页面刷新后前端仍能恢复请求头。退出登录时设置`sessions.revoked_at`，服务端立即拒绝该会话。

Cookie固定使用这些属性：

```http
Set-Cookie: ba_session=...; HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age=1209600
Set-Cookie: ba_csrf=...; Secure; SameSite=Lax; Path=/; Max-Age=1209600
```

除注册和登录外，所有写请求必须带`X-CSRF-Token`。后端验证CSRF令牌、Origin和Referer，跨站来源直接拒绝。CORS默认关闭，只允许同源访问API。

管理员授权、封禁和删除会话属于高风险操作，要求当前管理员在10分钟内通过`POST /api/admin/reauth`完成密码确认。确认成功后更新`sessions.admin_confirmed_at`，超时后接口返回`403 admin_reauth_required`。

注册、登录、上传和发帖要做限流。MVP阶段使用进程内滑动窗口，键为真实IP和用户ID；后续多进程部署再迁移到SQLite表或Redis。登录失败5次后对同一IP短暂冷却，避免密码暴力尝试。

密码哈希使用libsodium的Argon2id。生产配置采用`crypto_pwhash_OPSLIMIT_MODERATE`和`crypto_pwhash_MEMLIMIT_MODERATE`起步，实际参数以服务器压测结果为准，单次登录耗时控制在200ms到800ms。

Markdown渲染前先遍历cmark AST。原始HTML节点直接删除；`a.href`只允许`http`、`https`和`mailto`协议；外链渲染后自动加`rel="nofollow noopener noreferrer"`；`img.src`只允许当前用户`upload_refs`记录对应的站内`/uploads/`路径。最终HTML只允许出现`p`、`br`、`blockquote`、`ul`、`ol`、`li`、`strong`、`em`、`code`、`pre`、`a`和`img`。

安全响应头由nginx统一设置：

```nginx
add_header X-Content-Type-Options nosniff always;
add_header Referrer-Policy same-origin always;
add_header X-Frame-Options DENY always;
add_header Content-Security-Policy "default-src 'self'; img-src 'self'; style-src 'self'; script-src 'self'; base-uri 'none'; frame-ancestors 'none'" always;
```

---

## 上传策略

上传只接受图片，MVP允许`image/jpeg`、`image/png`、`image/gif`和`image/webp`。单文件最大5MB，单用户每天最多上传100个文件。

后端不能信任浏览器传来的MIME和扩展名。处理流程为读取临时文件、计算SHA-256、根据文件头判断类型、尝试解码图片、读取宽高、生成最终扩展名、移动到目标目录、写入`uploads`和`upload_refs`表。若SHA-256已存在，则复用已有`uploads`记录，只补充当前用户的`upload_refs`记录。任一步失败都删除临时文件。

保存路径格式：

```text
/var/lib/blogalone/uploads/YYYY/MM/sha256前2位/sha256.扩展名
```

`uploads`记录物理文件，`upload_refs`记录用户对该文件的引用。同一SHA-256文件只保存一份；同一用户重复上传相同文件时直接返回已有引用。数据库只记录相对路径，前端引用`/uploads/YYYY/MM/...`。上传文件访问由后端处理，必须拒绝路径穿越，只按白名单MIME返回图片，未知类型按`application/octet-stream`处理，并设置`X-Content-Type-Options: nosniff`。

用户提交帖子时只能引用自己`upload_refs`记录关联的图片。帖子保存成功后设置`upload_refs.attached_at`。定时清理任务删除超过24小时仍未绑定内容的`upload_refs`记录，再删除没有任何引用的物理文件。

---

## 工程目录

```text
project/
├── CMakeLists.txt
├── cmake/
│   └── deps.cmake
├── config/
│   ├── config.windows.json
│   └── config.linux.json
├── src/
│   ├── main.cc
│   ├── controllers/
│   ├── filters/
│   ├── services/
│   ├── models/
│   ├── repositories/
│   ├── util/
│   └── plugins/
├── migrations/
│   └── 001_init.sql
├── tests/
│   ├── unit/
│   ├── integration/
│   └── fixtures/
├── web/
│   ├── pages/
│   │   ├── index.html
│   │   └── thread.html
│   └── static/
│       ├── css/retro.css
│       └── js/api.js
└── deploy/
    ├── nginx.conf
    ├── blogalone.service
    ├── backup.sh
    └── update.sh
```

`controllers/`每个文件对应一组路由，只做参数校验、调用服务、转换响应。页面控制器负责返回`web/pages/`下的HTML入口文件，静态资源控制器负责返回`web/static/`下的CSS和JavaScript。`services/`层只依赖仓储接口和事务接口，不接触HTTP请求和响应对象。`repositories/`封装SQL和DrogonORM模型，避免SQL散落在控制器里。`util/`包含Markdown AST清理、HTML渲染、密码处理、token生成和时间工具。`plugins/`负责迁移、索引检查和定时清理任务。

---

## 部署方式

```text
公网流量
  -> nginx:443，负责TLS、域名和反向代理
      /              -> proxy_pass http://127.0.0.1:8080
                       proxy_set_header Host $host
                       proxy_set_header X-Real-IP $remote_addr
                       proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for
                       proxy_set_header X-Forwarded-Proto $scheme
                       proxy_set_header X-Request-Id $request_id

本机进程
  -> systemd运行/opt/blogalone/blogalone --config /etc/blogalone/config.linux.json
```

`config.linux.json`里设置`trusted_proxies:["127.0.0.1"]`。Drogon读取`X-Forwarded-For`时，从右往左跳过可信代理，取第一个非可信地址作为真实客户端IP。若请求不是来自可信代理，忽略`X-Forwarded-For`，直接使用连接IP。

运行目录固定为：

```text
/opt/blogalone/                 # 二进制和web资源
/etc/blogalone/                 # 配置
/var/lib/blogalone/blogalone.db # SQLite数据库
/var/lib/blogalone/uploads/     # 上传文件
/var/log/blogalone/             # 应用日志
```

首次部署不开放公开接口创建管理员。本机执行`/opt/blogalone/blogalone admin create --username name --password-file /root/admin_password.txt`创建第一个管理员；若系统中已有管理员，该命令默认拒绝执行，除非显式传入`--force`并写入审计日志。

更新流程为上传新二进制到临时文件、执行`--check-config`、停止服务、替换二进制、启动服务、访问`/api/healthz`。失败时恢复旧二进制并启动服务。该流程不做热更新，停机时间通常只有几秒。

`/api/healthz`不需要登录，只检查进程存活、配置已加载、数据库可执行轻量查询。不要在健康检查里做写入、迁移或外部网络请求。

---

## 备份和恢复

数据库备份使用SQLite在线备份能力，不能直接复制运行中的`blogalone.db`主文件。`backup.sh`执行`sqlite3 /var/lib/blogalone/blogalone.db ".backup '/backup/blogalone/YYYYMMDD-HHMMSS.db'"`，再打包上传目录。

保留策略为每日备份7份、每周备份4份。每次部署和迁移前强制生成一次备份。恢复演练至少做一次：在临时目录恢复数据库和上传文件，启动服务并访问板块列表、帖子详情和图片地址。

日志保留30天。日志字段包含时间、级别、request_id、真实IP、user_id、HTTP方法、路径、状态码、耗时和错误码。密码、Cookie、CSRF令牌、邮箱完整地址不能写入日志。

---

## 测试策略

单元测试针对服务层。简单业务可使用单连接`:memory:`SQLite；涉及事务、WAL、并发回帖和迁移的测试必须使用临时文件数据库，避免多连接内存库行为和线上不一致。

服务层测试覆盖：

- 注册重名用户名和邮箱
- 密码哈希校验和登录失败
- 封禁用户无法发帖、回帖和上传
- 发帖、回帖、楼中楼回复的事务结果
- 并发回帖时楼层号唯一
- 普通用户只能编辑和删除自己的内容
- 管理员删除、恢复、置顶、加精和封禁写入审计日志
- 管理员授权要求二次确认，移除自己和仅剩的管理员会被拒绝
- Markdown AST清理拒绝`script`、`javascript:`链接、外链图片和危险属性
- 上传拒绝超大文件、伪造MIME、无法解码图片和未绑定孤儿文件

集成测试在`SetUp`阶段启动Drogon并绑定随机端口，再用DrogonHttpClient发起真实HTTP请求。覆盖范围包括页面路由、静态资源、API路由、过滤器、Cookie属性、CSRF拒绝、分页边界、错误响应格式、数据库写入和上传下载链路。

页面测试以最小可用为目标。核心页面包括板块列表、帖子列表、帖子详情、登录注册、发帖、回帖、上传图片和管理员操作入口。每个页面要处理加载中、空状态、错误状态和未登录状态。

---

## MVP边界

MVP必须完成：

- 用户注册、登录、退出和会话失效
- 板块列表、帖子列表、帖子详情
- 发帖、回帖、楼中楼回复
- Markdown渲染和安全清理
- 图片上传和站内引用
- 管理员置顶、加精、删除、恢复、封禁和授权管理
- SQLite迁移、后端页面服务、备份脚本、systemd服务、nginx代理配置
- 单元测试和集成测试覆盖核心链路

暂不实现：

- 私信
- 通知中心
- 全文搜索
- OAuth登录
- 热更新
- 多机部署

---

## 待定问题

Linux二进制来源仍需在MVP开发前定稿。

建议MVP阶段在Linux服务器上直接拉代码、本地编译。这个方式依赖最少，缺点是首次编译较慢。开发机通过WSL2或交叉编译工具链生成Linux二进制也可行，速度更快，但需要额外维护构建环境。部署流程稳定后，再把构建和发布放进CI。
