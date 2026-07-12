# BlogAlone全栈前端开发提示词

```text
你是一名资深全栈工程师和Web界面设计师，擅长C++20、Drogon、SQLite、原生HTML、CSS与JavaScript。请直接在当前BlogAlone仓库中完成前端及必要的后端补充，不要只给方案或代码片段。目标是交付可运行、可测试的完整论坛MVP。

<project_context>
BlogAlone是一个小型中文贴吧式论坛。后端使用C++20、Drogon、SQLite、libsodium、cmark-gfm和spdlog。阶段1至阶段9已经完成，Windows与Ubuntu 26.04均已完成依赖、应用、单元测试和集成测试构建。现有Linux CTest记录共128项测试，128项通过，0项失败。

请先完整阅读README.md、note.md、DEVELOPMENT_ROADMAP.md以及相关控制器、服务、仓储、测试和web目录。以实际代码为准，不要只依赖本文中的接口摘要。当前工作区可能包含用户未提交的修改，保留这些修改，不要覆盖、回退或重排无关代码。

现有前端位于web目录，只能浏览板块、主题和回复，尚未覆盖完整业务。后端直接提供HTML入口、同源静态资源、上传文件和JSON API。生产CSP只允许同源脚本、样式与图片。
</project_context>

<primary_goal>
完成简体中文的全功能论坛前端，覆盖访客、登录用户和管理员的完整工作流。允许补充实现管理后台闭环所必需的后端接口，但不要扩展与MVP无关的业务。

实现完成后，用户应能完成以下操作：

1. 访客浏览板块、主题列表、置顶与加精状态、主题详情、楼层和楼中楼回复，并使用分页。
2. 用户注册、登录、退出，刷新后恢复会话，查看和修改个人邮箱与头像。
3. 登录用户创建主题、回复楼层、回复楼中楼，编辑或删除自己的主题、楼层和楼中楼。
4. 编辑内容时使用Markdown工具栏，显示字符计数，支持选择、拖放和粘贴图片，展示上传状态，并把成功上传的站内URL插入Markdown。
5. 发帖、回复和编辑表单使用localStorage自动保存草稿。提交成功后清除对应草稿；草稿键必须按用户、页面和目标内容隔离。
6. 管理员维护板块，管理用户角色与封禁状态，置顶、加精、删除和恢复内容，查看审计日志，查看并撤销会话。
7. 高风险管理操作收到admin_reauth_required时，弹出密码确认界面，调用重认证接口，成功后仅重试原操作一次。
</primary_goal>

<frontend_architecture>
继续使用原生HTML5、CSS和现代JavaScript，不引入React、Vue、Node构建链或客户端路由框架。可以采用真正有必要的轻量库，但依赖文件必须纳入仓库并从web/static/vendor同源加载，不得在运行时访问CDN。优先使用浏览器原生能力，避免为简单功能引入依赖。

扩展PageController，为下列可分享URL提供独立HTML入口：

- /：论坛首页与板块总览
- /forums/:slug：指定板块的主题列表
- /threads/:id：主题详情、楼层与楼中楼
- /login：登录
- /register：注册
- /compose：创建主题，通过查询参数预选板块
- /profile：个人资料
- /admin：管理后台

页面可以共享页头、页脚和脚本模块，但当前服务直接返回静态HTML，不要引入服务端模板系统。通过小型、职责清晰的ES模块组织代码。避免把全部逻辑继续堆进一个api.js。建议按实际复杂度拆分为API客户端、会话状态、公共DOM工具、编辑器、页面入口和管理功能模块。

主要改动目录：

- web/pages：独立HTML入口
- web/static/css：设计令牌、全局样式、页面与组件样式
- web/static/js：API客户端、状态、编辑器和页面逻辑
- web/static/vendor：确有必要的本地化第三方依赖
- src/controllers/page_controller.cc：新增页面入口
- src/controllers、src/services、src/repositories、src/models：仅用于补齐必要管理接口
- tests/unit与tests/integration：后端回归和HTTP契约测试
- 必要时新增tests/e2e或等价浏览器测试目录，并接入可重复运行的测试方式
</frontend_architecture>

<verified_api_contract>
现有接口如下。请求字段、响应字段、状态码仍需在实现前对照控制器和集成测试复核。

公开接口：

- GET /api/healthz
- GET /api/forums，返回{items: Forum[]}
- GET /api/forums/:slug/threads?page&page_size，返回分页对象
- GET /api/threads/:id?page&page_size，返回{thread, posts: 分页对象}
- POST /api/auth/register，请求{username, email?, password}，返回{user, csrf_token, expires_at}
- POST /api/auth/login，请求{username, password}，返回{user, csrf_token, expires_at}

登录接口：

- GET /api/me，返回{user}
- PATCH /api/me，请求{email?, avatar_url?}，返回{user}
- POST /api/auth/logout，成功返回204
- POST /api/threads，请求{forum_slug, title, body_md}，成功返回201及{thread}
- PATCH /api/threads/:id，请求{title, body_md}，返回{thread}
- DELETE /api/threads/:id，成功返回204
- POST /api/threads/:id/posts，请求{body_md}，成功返回201及{post}
- PATCH /api/posts/:id，请求{body_md}，返回{post}
- DELETE /api/posts/:id，成功返回204
- POST /api/posts/:id/sub_posts，请求{body_md, reply_to_user_id?}，成功返回201及{sub_post}
- PATCH /api/sub_posts/:id，请求{body_md}，返回{sub_post}
- DELETE /api/sub_posts/:id，成功返回204
- POST /api/uploads，使用multipart/form-data，唯一文件字段名为file，成功返回201及{upload:{url,mime,size,width,height}}

管理员接口：

- POST /api/admin/forums，请求{slug,name,description,sort_order}，返回201及{forum}
- PATCH /api/admin/forums/:id，请求可包含{slug,name,description,sort_order}，返回{forum}
- DELETE /api/admin/forums/:id，成功返回204
- GET /api/admin/users?role&page&page_size，role为user或admin，返回分页对象
- POST /api/admin/reauth，请求{password}，返回{admin_confirmed_at}
- PATCH /api/admin/threads/:id/pin，请求{is_pinned:boolean}
- PATCH /api/admin/threads/:id/feature，请求{is_featured:boolean}
- PATCH /api/admin/threads/:id/delete，请求{is_deleted:boolean}
- PATCH /api/admin/posts/:id/delete，请求{is_deleted:boolean}
- PATCH /api/admin/sub_posts/:id/delete，请求{is_deleted:boolean}
- PATCH /api/admin/users/:id/role，请求{role:"user"|"admin"}，返回{user}
- PATCH /api/admin/users/:id/ban，请求{banned_until:Unix秒|null}，返回{user}
- DELETE /api/admin/sessions/:token_hash，成功返回204
- GET /api/admin/audit_logs?page&page_size，返回分页对象

分页对象统一为{items,page,page_size,total}。page最小为1，page_size默认20，最大50。

主要响应模型：

- User：id、username、role、email、avatar_url、created_at、updated_at；管理员用户模型另含banned_until
- Forum：id、slug、name、description、sort_order、created_at、updated_at
- Thread：id、forum、author、title、body_md、body_html、is_pinned、is_featured、reply_count、last_reply_at、last_reply_user_id、created_at、updated_at
- Post：id、thread_id、author、floor_no、body_md、body_html、created_at、updated_at、sub_posts
- SubPost：id、post_id、thread_id、author、body_md、body_html、reply_to_user_id、reply_to_username、created_at、updated_at

错误响应统一为{error:{code,message,request_id}}。前端用code决定行为，用message提供具体反馈，不要解析message做分支。必须处理invalid_argument、unauthenticated、forbidden、admin_reauth_required、not_found、conflict、rate_limited和internal_error。

认证使用ba_session HttpOnly Cookie和可读的ba_csrf Cookie。fetch必须使用credentials:"same-origin"。除注册和登录外，POST、PATCH、PUT和DELETE都带X-CSRF-Token。不要把会话令牌、CSRF令牌、密码或完整认证响应写入localStorage、日志或页面调试信息。
</verified_api_contract>

<backend_gaps>
补齐管理员前端闭环所需的最小后端能力。目前撤销会话接口需要token_hash，却没有会话列表接口；公开内容查询会过滤软删除记录，也没有管理员查看已删除内容的列表，因此无法从前端恢复不可见内容。

请先设计并实现清晰、分页、可测试的管理查询接口：

1. 管理员会话列表。至少支持按用户筛选，返回撤销操作需要的稳定标识，以及用户名、创建时间、过期时间、撤销时间、IP和User-Agent等管理判断所需字段。不要返回明文会话令牌。
2. 已删除内容列表。至少能分别查询主题、楼层和楼中楼，返回恢复操作所需ID及足够的上下文，包括作者、所属板块或主题、楼层号、内容摘要、删除时间与删除者。

接口命名和响应结构应服从现有/admin路由、分页对象、错误映射、controller-service-repository分层与AdminService模式。先检查数据库现有字段和索引。若列表查询需要的新索引具有明确价值，通过新迁移添加，不要修改已经应用的迁移文件。同步更新note.md中的API说明。

所有新增管理员接口必须挂载认证和管理员过滤器。会话撤销、用户角色与封禁继续要求10分钟内的管理员二次确认。不要降低现有CSRF、权限、审计或日志脱敏要求。
</backend_gaps>

<interaction_requirements>
全站必须真实可用，不保留无响应按钮、占位链接或只展示不执行的表单。

- 页面加载时显示稳定的加载状态，失败时提供页面内错误与重试操作，空列表提供对应空状态。
- 未登录用户触发写操作时跳转登录页，并使用同源、经过校验的return_to参数返回原页面。
- 登录状态以GET /api/me为准。401时清理内存状态并进入访客界面，不循环请求。
- 创建主题成功后进入主题详情；创建回复成功后刷新对应页并定位新内容；编辑成功后就地刷新内容；删除前要求明确确认。
- 只向内容作者展示普通编辑和删除操作。管理员操作根据user.role显示，后端仍是最终权限边界。
- 分页使用真实total计算页数。保留page与page_size查询参数，使URL可复制并支持浏览器前进后退。
- 时间以用户本地时区显示，并使用time元素保存原始Unix时间。对近期时间可以显示相对描述，但必须提供完整时间。
- Markdown正文只渲染后端返回的body_html。不要在客户端重新清理或改写服务端HTML，也不要用客户端Markdown预览替代最终提交结果。
- 本次不要求实时Markdown预览。编辑器提供工具栏、字符计数、图片上传插入和草稿恢复即可。
- 图片上传支持JPEG、PNG、GIF和WebP。前端提前提示5MB和最大5792像素限制，但后端校验结果始终为准。上传失败不能破坏编辑器已有文本。
- 个人头像复用上传接口，上传成功后把返回url提交到PATCH /api/me。
- 表单展示服务端字段级限制：用户名3至32个字符，密码8至128个字符，主题标题1至80个字符，主题和楼层正文1至20000个字符，楼中楼正文1至2000个字符。
- 管理后台至少包含板块、用户、已删除内容、会话和审计日志五个视图，采用标签或紧凑导航切换。长列表使用表格或定义列表，不使用卡片网格。
- 封禁操作提供常用时长选择和精确日期时间输入，解除封禁发送null。角色降级、自我相关高风险操作和删除板块必须有清楚的确认文案。
- 适当使用dialog元素或可靠的无依赖模态框方案，处理焦点进入、焦点返回、Escape关闭和背景滚动。
</interaction_requirements>

<visual_direction>
视觉参考2000年前后的中文BBS和门户论坛，但只复刻信息组织与视觉语言，不复刻旧浏览器缺陷。代码面向当前Chrome、Edge、Firefox和Safari。

核心特征：

- 亮色单主题，宽屏高密度布局。桌面主体宽度约1100至1280px，手机端自然重排，不做固定桌面画布缩放。
- 纯文字BlogAlone站名，不制作Hero、大图、插画、渐变背景、装饰光斑或营销文案。
- 以白色、浅灰、灰蓝表头、深色正文和经典蓝色链接构成3至5色体系。访问过的内容链接可以有可辨识状态。危险操作使用克制的暗红色。
- 字体优先使用Tahoma、Verdana、宋体或可用的中文系统宋体组合。正文约13至14px，辅助信息约12px，标题按信息层级适度放大。字距固定为0。
- 信息层级依靠细边框表格、分栏、标题栏、链接、缩进、编号和留白建立。少用卡片，不使用卡片套卡片，不使用大圆角、胶囊标签、悬浮阴影和现代SaaS仪表盘风格。
- 圆角为0至3px。边框以1px实线为主。按钮可以有早期网页的轻微立体边框感，但必须保持清晰的按下、禁用和焦点状态。
- 首页应像论坛索引：页头、实用导航、板块目录、主题列表和站点状态信息在首屏形成高密度结构。主题列表突出标题、作者、回复数、更新时间、置顶和加精状态。
- 主题详情接近传统论坛楼层布局。桌面端每层可以采用窄作者栏加宽正文栏；移动端改为作者信息横排置顶。楼中楼在楼层内部以更紧凑的缩进列表呈现。
- 管理后台像传统后台管理表格，强调扫描、排序、分页与批量判断，不做装饰性统计大卡片。
- 导航、按钮和工具操作优先使用熟悉的文字链接或小型符号。不要使用emoji。需要图标时采用本地化的轻量图标库或CSS可表达的简单符号，并提供aria-label或title。
- 可以加入极少量符合时代气质的细节，如双线标题边框、浅色斑马纹、当前位置面包屑、站点公告栏，但不要加入假计数器、跑马灯、闪烁文本或影响阅读的怀旧噱头。

响应式不是把桌面表格整体压缩。窄屏下隐藏次要列、把行转换为紧凑的分组信息、扩大可点击区域，并确保编辑器、模态框、分页和管理操作不横向溢出。所有交互保持键盘可达，焦点轮廓清晰，对比度达到WCAG AA基础要求。
</visual_direction>

<implementation_constraints>
- 保持现有C++20风格、controller-service-repository边界、统一错误格式和事务策略。
- 手工修改文件时保持改动集中，不重构无关后端代码。
- 不修改已应用迁移的内容；需要变更数据库结构时新增迁移。
- 不增加Node.js构建要求。页面必须由Drogon直接提供。
- 禁止内联脚本和内联样式，确保生产CSP default-src 'self'、img-src 'self'、style-src 'self'、script-src 'self'可用。
- 不依赖外部字体、CDN、远程图片或第三方在线服务。
- 不把用户提供的字符串通过innerHTML插入页面。innerHTML仅可用于后端已经安全渲染的body_html字段。
- API客户端应抛出或返回包含HTTP状态、error.code、error.message和request_id的结构化错误，供页面统一处理。
- 防止重复提交。请求期间禁用对应提交按钮，并保持布局尺寸稳定。
- 使用语义HTML、显式label、正确的button类型、合理标题层级、aria-live错误区域和skip link。
- 处理prefers-reduced-motion。动效只用于必要的状态过渡，持续时间短，不改变版面尺寸。
</implementation_constraints>

<testing_and_validation>
实现过程中采用小步修改和聚焦验证。后端改动后先运行相关单元或集成测试，再继续扩展前端。

交付前必须完成：

1. 构建blogalone、blogalone_unit_tests和blogalone_integration_tests。
2. 运行全部CTest，保持现有128项测试通过，并让新增测试通过。
3. 为新增管理查询接口补充仓储或服务单元测试及HTTP集成测试，覆盖认证、管理员权限、分页、空状态、筛选、恢复和二次确认。
4. 使用隔离的临时数据库和测试夹具创建管理员、普通用户、板块、主题、楼层、楼中楼、上传、软删除内容和会话。不要依赖或修改开发者真实数据库。
5. 启动实际BlogAlone服务，完成浏览器端到端验证：注册、登录、退出、个人资料、头像、发帖、楼层回复、楼中楼、编辑、删除、上传、分页、管理员二次确认、板块管理、用户管理、内容恢复、会话撤销和审计日志。
6. 使用Playwright检查至少1440x900、1024x768、390x844和360x800视口。保存必要截图并检查页面没有横向溢出、文本遮挡、操作按钮错位、空白画布或不可关闭模态框。
7. 完成键盘导航、焦点管理、表单标签、颜色对比度和缩放至200%的基础可访问性检查。
8. 检查浏览器控制台与网络请求，不能存在未处理异常、404静态资源、跨源请求、重复写请求或敏感信息泄漏。

若现有环境缺少浏览器测试框架，可以新增最小且可复现的测试配置。不要为了测试引入前端生产构建链。若某项验证受环境限制无法执行，在最终报告中准确说明限制与已完成的替代检查。
</testing_and_validation>

<work_sequence>
1. 阅读文档与相关代码，列出已验证的现有契约、后端缺口和计划改动文件。
2. 先补齐管理查询API及自动化测试，保持范围最小。
3. 扩展页面控制器与HTML入口，建立共享CSS和JavaScript模块。
4. 按公开浏览、认证与资料、内容写入、编辑器上传、管理后台的顺序完成工作流，每完成一组就做聚焦验证。
5. 启动服务进行端到端和响应式浏览器验证，修复实际问题。
6. 更新必要文档，给出简洁的交付报告，列出修改内容、后端新增契约、测试结果和仍存在的明确限制。
</work_sequence>

<success_criteria>
只有在完整MVP流程可以通过真实后端运行、所有自动化测试通过、桌面和手机视口均经过浏览器验证、没有占位交互或已知严重可访问性问题时，任务才算完成。完成前请逐项对照primary_goal、interaction_requirements、implementation_constraints和testing_and_validation自检。
</success_criteria>
```