import { el, clear, loadingState, emptyState, errorState, timeElement, buildPagination, queryParam, safeReturnTo } from "./dom-utils.js";
import { api, ApiError } from "./api-client.js";
import { loadSession, getCurrentUser, redirectToLogin } from "./session-state.js";
import { MarkdownEditor } from "./editor.js";
import { confirmDialog } from "./modal.js";

const THREAD_TITLE_MAX = 80;
const BODY_MAX = 20000;
const SUB_POST_MAX = 2000;

function threadId() {
  return window.location.pathname.split("/").filter(Boolean).at(-1);
}

function currentQuery() {
  return { page: queryParam("page", "1"), pageSize: queryParam("page_size", "20") };
}

function currentPath() {
  return window.location.pathname + window.location.search;
}

function requireLogin() {
  const user = getCurrentUser();
  if (!user) {
    redirectToLogin(safeReturnTo(currentPath()));
    return null;
  }
  return user;
}

async function runButtonAction(button, action, failureMessage) {
  button.disabled = true;
  try {
    await action();
  } catch (error) {
    window.alert(error instanceof ApiError ? error.message : failureMessage);
  } finally {
    button.disabled = false;
  }
}

function flagTags(thread) {
  const tags = [];
  if (thread.is_pinned) {
    tags.push(el("span", { className: "ba-tag ba-tag-pinned" }, ["置顶"]));
  }
  if (thread.is_featured) {
    tags.push(el("span", { className: "ba-tag ba-tag-featured" }, ["精华"]));
  }
  return tags;
}

function bodyHtmlNode(html) {
  const container = el("div", { className: "ba-floor-body" });
  // Only backend-rendered, AST-sanitized HTML is ever assigned via innerHTML.
  container.innerHTML = html;
  return container;
}

function inlineEditorForm({ initialValue, maxLength, draftKey, submitLabel, onSubmit, onCancel, placeholder }) {
  const wrapper = el("div", { className: "ba-form-wide" });
  const mount = el("div");
  const errorBox = el("p", { className: "ba-field-error", role: "alert" });
  const submitButton = el("button", { type: "button", className: "ba-btn ba-btn-primary" }, [submitLabel]);
  const cancelButton = el("button", { type: "button", className: "ba-btn" }, ["取消"]);
  wrapper.append(mount);

  const editor = new MarkdownEditor({
    mountPoint: mount,
    maxLength,
    draftKey,
    initialValue,
    placeholder,
  });

  submitButton.addEventListener("click", async () => {
    const value = editor.getValue().trim();
    if (!value) {
      errorBox.textContent = "内容不能为空";
      return;
    }
    if ([...value].length > maxLength) {
      errorBox.textContent = `内容超过${maxLength}字符限制`;
      return;
    }
    submitButton.disabled = true;
    errorBox.textContent = "";
    try {
      await onSubmit(value);
      editor.clearDraft();
    } catch (error) {
      errorBox.textContent = error instanceof ApiError ? error.message : "提交失败";
    } finally {
      submitButton.disabled = false;
    }
  });
  cancelButton.addEventListener("click", () => onCancel());

  wrapper.append(errorBox, el("div", { className: "ba-form-actions" }, [submitButton, cancelButton]));
  return wrapper;
}

async function render() {
  const root = document.querySelector("[data-thread-root]");
  clear(root);
  root.append(loadingState("正在加载主题..."));

  let session;
  try {
    session = await loadSession().catch(() => null);
  } catch (error) {
    session = null;
  }

  const id = threadId();
  const { page, pageSize } = currentQuery();

  try {
    const detail = await api.get(`/api/threads/${encodeURIComponent(id)}?page=${page}&page_size=${pageSize}`);
    document.title = `${detail.thread.title} - BlogAlone`;
    document.querySelector("[data-thread-crumb]").textContent = detail.thread.title;
    const forumLink = document.querySelector("[data-forum-link]");
    forumLink.href = `/forums/${encodeURIComponent(detail.thread.forum.slug)}`;
    forumLink.textContent = detail.thread.forum.name;

    clear(root);
    root.append(buildThreadPanel(detail, page, pageSize));
  } catch (error) {
    clear(root);
    const message = error instanceof ApiError ? error.message : "主题加载失败";
    root.append(errorState(message, render));
  }
}

function buildThreadPanel(detail, page, pageSize) {
  const user = getCurrentUser();
  const { thread } = detail;
  const isAdmin = Boolean(user && user.role === "admin");
  const isAuthor = Boolean(user && user.id === thread.author.id);

  const titleRow = el("div", { className: "ba-panel-title" }, [
    el("div", {}, [
      el("h1", { className: "ba-thread-detail-title" }, [thread.title]),
      el("div", { className: "ba-thread-flags" }, flagTags(thread)),
    ]),
    el("span", { className: "ba-status-pill" }, [`${thread.reply_count} 回复`]),
  ]);

  const meta = el("p", { className: "ba-col-secondary" }, [
    `作者:${thread.author.username} · 发表于 `,
    timeElement(thread.created_at),
  ]);

  const bodyContainer = el("div", {});
  const editHolder = el("div", {});
  function renderBody() {
    clear(bodyContainer);
    bodyContainer.append(bodyHtmlNode(thread.body_html));
  }
  renderBody();

  const actions = el("div", { className: "ba-floor-actions" });
  if (isAuthor) {
    const editButton = el("button", { type: "button", className: "ba-btn ba-btn-small" }, ["编辑主题"]);
    const deleteButton = el("button", { type: "button", className: "ba-btn ba-btn-small ba-btn-danger" }, ["删除主题"]);
    editButton.addEventListener("click", () => openThreadEditForm());
    deleteButton.addEventListener("click", async () => {
      const confirmed = await confirmDialog(`确定要删除主题《${thread.title}》吗?此操作可由管理员恢复。`);
      if (!confirmed) {
        return;
      }
      await runButtonAction(deleteButton, async () => {
        await api.delete(`/api/threads/${thread.id}`);
        window.location.assign(`/forums/${encodeURIComponent(thread.forum.slug)}`);
      }, "删除失败");
    });
    actions.append(editButton, deleteButton);
  }
  if (isAdmin) {
    const pinButton = el("button", { type: "button", className: "ba-btn ba-btn-small" }, [
      thread.is_pinned ? "取消置顶" : "置顶",
    ]);
    const featureButton = el("button", { type: "button", className: "ba-btn ba-btn-small" }, [
      thread.is_featured ? "取消加精" : "加精",
    ]);
    const adminDeleteButton = el("button", { type: "button", className: "ba-btn ba-btn-small ba-btn-danger" }, [
      "管理员删除",
    ]);
    pinButton.addEventListener("click", async () => {
      await runButtonAction(pinButton, async () => {
        await api.patch(`/api/admin/threads/${thread.id}/pin`, { is_pinned: !thread.is_pinned });
        await render();
      }, "置顶操作失败");
    });
    featureButton.addEventListener("click", async () => {
      await runButtonAction(featureButton, async () => {
        await api.patch(`/api/admin/threads/${thread.id}/feature`, { is_featured: !thread.is_featured });
        await render();
      }, "加精操作失败");
    });
    adminDeleteButton.addEventListener("click", async () => {
      const confirmed = await confirmDialog(`确定要以管理员身份删除主题《${thread.title}》吗?`);
      if (!confirmed) {
        return;
      }
      await runButtonAction(adminDeleteButton, async () => {
        await api.patch(`/api/admin/threads/${thread.id}/delete`, { is_deleted: true });
        window.location.assign(`/forums/${encodeURIComponent(thread.forum.slug)}`);
      }, "管理员删除失败");
    });
    actions.append(pinButton, featureButton, adminDeleteButton);
  }

  function openThreadEditForm() {
    clear(editHolder);
    const titleInput = el("input", { type: "text", value: thread.title, maxlength: String(THREAD_TITLE_MAX) });
    const titleField = el("div", { className: "ba-field" }, [
      el("label", { for: "thread-edit-title" }, ["标题(1-80字符)"]),
      titleInput,
    ]);
    titleInput.id = "thread-edit-title";
    const form = inlineEditorForm({
      initialValue: thread.body_md,
      maxLength: BODY_MAX,
      draftKey: user ? `ba:draft:${user.id}:thread:${thread.id}:edit` : "",
      submitLabel: "保存修改",
      placeholder: "支持Markdown格式",
      onSubmit: async (value) => {
        const title = titleInput.value.trim();
        if (!title || [...title].length > THREAD_TITLE_MAX) {
          throw new ApiError(400, "invalid_argument", "标题需为1-80个字符", "");
        }
        await api.patch(`/api/threads/${thread.id}`, { title, body_md: value });
        clear(editHolder);
        render();
      },
      onCancel: () => clear(editHolder),
    });
    editHolder.append(titleField, form);
  }

  const floorsSection = el("section", { className: "ba-panel", "aria-labelledby": "floors-title" }, [
    el("div", { className: "ba-panel-title" }, [
      el("h2", { id: "floors-title" }, ["回复"]),
      el("span", { className: "ba-status-pill" }, [`${detail.posts.total} 层`]),
    ]),
  ]);
  const floorsBody = el("div", { className: "ba-panel-body" });
  floorsSection.append(floorsBody);

  if (detail.posts.items.length === 0) {
    floorsBody.append(emptyState("暂无回复,来发表第一个回复吧。"));
  } else {
    const list = el("div", { className: "ba-floor-list" });
    for (const post of detail.posts.items) {
      list.append(buildFloor(post, thread, user));
    }
    floorsBody.append(list);
    floorsBody.append(
      buildPagination(page, pageSize, detail.posts.total, (targetPage) =>
        `/threads/${thread.id}?page=${targetPage}&page_size=${pageSize}`
      )
    );
  }

  const replySection = el("section", { className: "ba-panel" }, [
    el("div", { className: "ba-panel-title" }, [el("h2", {}, ["发表回复"])]),
  ]);
  const replyBody = el("div", { className: "ba-panel-body" });
  replySection.append(replyBody);
  if (user) {
    replyBody.append(
      inlineEditorForm({
        initialValue: "",
        maxLength: BODY_MAX,
        draftKey: `ba:draft:${user.id}:thread:${thread.id}:reply`,
        submitLabel: "发表回复",
        placeholder: "支持Markdown格式,可拖拽或粘贴图片",
        onSubmit: async (value) => {
          await api.post(`/api/threads/${thread.id}/posts`, { body_md: value });
          render();
        },
        onCancel: () => render(),
      })
    );
  } else {
    replyBody.append(
      el("p", {}, [
        "请先",
        el("a", { href: `/login?return_to=${encodeURIComponent(currentPath())}` }, ["登录"]),
        "后参与回复。",
      ])
    );
  }

  const panel = el("section", { className: "ba-panel" }, [
    titleRow,
    el("div", { className: "ba-panel-body" }, [meta, bodyContainer, editHolder, actions]),
  ]);

  const wrap = el("div", {}, [panel, floorsSection, replySection]);
  return wrap;
}

function buildFloor(post, thread, user) {
  const isAuthor = Boolean(user && user.id === post.author.id);
  const isAdmin = Boolean(user && user.role === "admin");

  const meta = el("div", { className: "ba-floor-author" }, [
    el("span", { className: "ba-username" }, [post.author.username]),
    el("span", { className: "ba-floor-no" }, [`#${post.floor_no}`]),
    timeElement(post.created_at),
  ]);

  const bodyContainer = el("div", {});
  bodyContainer.append(bodyHtmlNode(post.body_html));
  const editHolder = el("div", {});
  const replyHolder = el("div", {});

  const actions = el("div", { className: "ba-floor-actions" });
  const replyButton = el("button", { type: "button", className: "ba-btn ba-btn-small" }, ["回复"]);
  replyButton.addEventListener("click", () => {
    if (!requireLogin()) {
      return;
    }
    if (replyHolder.childElementCount > 0) {
      clear(replyHolder);
      return;
    }
    clear(replyHolder);
    replyHolder.append(
      inlineEditorForm({
        initialValue: "",
        maxLength: SUB_POST_MAX,
        draftKey: `ba:draft:${user.id}:post:${post.id}:subpost`,
        submitLabel: "发表楼中楼回复",
        placeholder: "楼中楼回复,最多2000字符",
        onSubmit: async (value) => {
          await api.post(`/api/posts/${post.id}/sub_posts`, {
            body_md: value,
            reply_to_user_id: post.author.id,
          });
          render();
        },
        onCancel: () => clear(replyHolder),
      })
    );
  });
  actions.append(replyButton);

  if (isAuthor) {
    const editButton = el("button", { type: "button", className: "ba-btn ba-btn-small" }, ["编辑"]);
    const deleteButton = el("button", { type: "button", className: "ba-btn ba-btn-small ba-btn-danger" }, ["删除"]);
    editButton.addEventListener("click", () => {
      clear(editHolder);
      editHolder.append(
        inlineEditorForm({
          initialValue: post.body_md,
          maxLength: BODY_MAX,
          draftKey: `ba:draft:${user.id}:post:${post.id}:edit`,
          submitLabel: "保存修改",
          onSubmit: async (value) => {
            await api.patch(`/api/posts/${post.id}`, { body_md: value });
            render();
          },
          onCancel: () => clear(editHolder),
        })
      );
    });
    deleteButton.addEventListener("click", async () => {
      const confirmed = await confirmDialog(`确定要删除 #${post.floor_no} 楼吗?`);
      if (!confirmed) {
        return;
      }
      await runButtonAction(deleteButton, async () => {
        await api.delete(`/api/posts/${post.id}`);
        await render();
      }, "删除失败");
    });
    actions.append(editButton, deleteButton);
  }
  if (isAdmin && !isAuthor) {
    const adminDelete = el("button", { type: "button", className: "ba-btn ba-btn-small ba-btn-danger" }, [
      "管理员删除",
    ]);
    adminDelete.addEventListener("click", async () => {
      const confirmed = await confirmDialog(`确定要以管理员身份删除 #${post.floor_no} 楼吗?`);
      if (!confirmed) {
        return;
      }
      await runButtonAction(adminDelete, async () => {
        await api.patch(`/api/admin/posts/${post.id}/delete`, { is_deleted: true });
        await render();
      }, "管理员删除失败");
    });
    actions.append(adminDelete);
  }

  const subList = el("div", { className: "ba-sub-post-list" });
  for (const subPost of post.sub_posts) {
    subList.append(buildSubPost(subPost, user));
  }

  return el("article", { className: "ba-floor" }, [
    meta,
    el("div", {}, [bodyContainer, editHolder, actions, replyHolder, subList]),
  ]);
}

function buildSubPost(subPost, user) {
  const isAuthor = Boolean(user && user.id === subPost.author.id);
  const isAdmin = Boolean(user && user.role === "admin");
  const metaChildren = [
    el("span", { className: "ba-username" }, [subPost.author.username]),
    timeElement(subPost.created_at),
  ];
  if (subPost.reply_to_username) {
    metaChildren.push(el("span", { className: "ba-reply-target" }, [`回复 @${subPost.reply_to_username}`]));
  }
  const meta = el("div", { className: "ba-sub-post-meta" }, metaChildren);
  const bodyContainer = el("div", { className: "ba-sub-post-body" });
  bodyContainer.innerHTML = subPost.body_html;
  const editHolder = el("div", {});

  const actions = el("div", { className: "ba-floor-actions" });
  if (isAuthor) {
    const editButton = el("button", { type: "button", className: "ba-btn ba-btn-small" }, ["编辑"]);
    const deleteButton = el("button", { type: "button", className: "ba-btn ba-btn-small ba-btn-danger" }, ["删除"]);
    editButton.addEventListener("click", () => {
      clear(editHolder);
      editHolder.append(
        inlineEditorForm({
          initialValue: subPost.body_md,
          maxLength: SUB_POST_MAX,
          draftKey: `ba:draft:${user.id}:subpost:${subPost.id}:edit`,
          submitLabel: "保存修改",
          onSubmit: async (value) => {
            await api.patch(`/api/sub_posts/${subPost.id}`, { body_md: value });
            render();
          },
          onCancel: () => clear(editHolder),
        })
      );
    });
    deleteButton.addEventListener("click", async () => {
      const confirmed = await confirmDialog("确定要删除这条楼中楼回复吗?");
      if (!confirmed) {
        return;
      }
      await runButtonAction(deleteButton, async () => {
        await api.delete(`/api/sub_posts/${subPost.id}`);
        await render();
      }, "删除失败");
    });
    actions.append(editButton, deleteButton);
  }
  if (isAdmin && !isAuthor) {
    const adminDelete = el("button", { type: "button", className: "ba-btn ba-btn-small ba-btn-danger" }, [
      "管理员删除",
    ]);
    adminDelete.addEventListener("click", async () => {
      const confirmed = await confirmDialog("确定要以管理员身份删除这条楼中楼回复吗?");
      if (!confirmed) {
        return;
      }
      await runButtonAction(adminDelete, async () => {
        await api.patch(`/api/admin/sub_posts/${subPost.id}/delete`, { is_deleted: true });
        await render();
      }, "管理员删除失败");
    });
    actions.append(adminDelete);
  }

  return el("div", { className: "ba-sub-post" }, [meta, bodyContainer, editHolder, actions]);
}

document.addEventListener("DOMContentLoaded", render);
