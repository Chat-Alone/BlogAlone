import { el, clear, loadingState, emptyState, errorState, timeElement } from "./dom-utils.js";
import { api, ApiError } from "./api-client.js";
import { loadSession, getCurrentUser, redirectToLogin } from "./session-state.js";
import { confirmDialog, passwordPromptDialog } from "./modal.js";

const PAGE_SIZE = 20;

// Retries an admin action exactly once after a successful password reauth.
async function withReauth(actionFn) {
  try {
    return await actionFn();
  } catch (error) {
    if (error instanceof ApiError && error.code === "admin_reauth_required") {
      const password = await passwordPromptDialog("此操作涉及敏感权限,请重新输入管理员密码确认身份。");
      if (!password) {
        throw error;
      }
      await api.post("/api/admin/reauth", { password });
      return actionFn();
    }
    throw error;
  }
}

function tableWrap(children) {
  return el("div", { className: "ba-table-wrap" }, children);
}

// Button-based pagination for the admin tab panels (no query-string routing
// inside a single tab; state lives in the enclosing closure).
function adminPagination(page, pageSize, total, onChange) {
  const totalPages = Math.max(1, Math.ceil(total / pageSize));
  const prev = el(
    "button",
    { type: "button", className: "ba-btn ba-btn-small", onClick: () => onChange(page - 1) },
    ["上一页"]
  );
  prev.disabled = page <= 1;
  const next = el(
    "button",
    { type: "button", className: "ba-btn ba-btn-small", onClick: () => onChange(page + 1) },
    ["下一页"]
  );
  next.disabled = page >= totalPages;
  return el("nav", { className: "ba-pagination", "aria-label": "分页" }, [
    prev,
    el("span", { className: "ba-page-current" }, [`第 ${page} / ${totalPages} 页`]),
    next,
    el("span", { className: "ba-col-secondary" }, [`共 ${total} 条`]),
  ]);
}

// ---------------------------------------------------------------- Forums --
function initForumsPanel(container) {
  async function load() {
    clear(container);
    container.append(loadingState("正在加载板块列表..."));
    try {
      const forums = await api.get("/api/forums");
      clear(container);
      container.append(buildCreateForm(), buildForumsTable(forums.items));
    } catch (error) {
      clear(container);
      container.append(errorState(error instanceof ApiError ? error.message : "加载失败", load));
    }
  }

  function buildCreateForm() {
    const slugInput = el("input", { type: "text", maxlength: "32", required: true });
    const nameInput = el("input", { type: "text", maxlength: "60", required: true });
    const descInput = el("input", { type: "text", maxlength: "200" });
    const sortInput = el("input", { type: "number", value: "0" });
    const errorBox = el("p", { className: "ba-field-error", role: "alert" });

    const form = el(
      "form",
      {
        className: "ba-admin-toolbar",
        onSubmit: async (event) => {
          event.preventDefault();
          errorBox.textContent = "";
          try {
            await api.post("/api/admin/forums", {
              slug: slugInput.value.trim(),
              name: nameInput.value.trim(),
              description: descInput.value.trim(),
              sort_order: Number.parseInt(sortInput.value, 10) || 0,
            });
            load();
          } catch (error) {
            errorBox.textContent = error instanceof ApiError ? error.message : "创建失败";
          }
        },
      },
      [
        el("div", { className: "ba-field" }, [el("label", {}, ["slug"]), slugInput]),
        el("div", { className: "ba-field" }, [el("label", {}, ["名称"]), nameInput]),
        el("div", { className: "ba-field" }, [el("label", {}, ["说明"]), descInput]),
        el("div", { className: "ba-field" }, [el("label", {}, ["排序"]), sortInput]),
        el("button", { type: "submit", className: "ba-btn ba-btn-primary" }, ["新建板块"]),
      ]
    );
    return el("div", {}, [form, errorBox]);
  }

  function buildForumsTable(forums) {
    if (forums.length === 0) {
      return emptyState("目前还没有任何板块。");
    }
    const rows = forums.map((forum) => {
      const nameInput = el("input", { type: "text", value: forum.name });
      const descInput = el("input", { type: "text", value: forum.description });
      const sortInput = el("input", { type: "number", value: String(forum.sort_order) });
      const saveButton = el("button", { type: "button", className: "ba-btn ba-btn-small" }, ["保存"]);
      const deleteButton = el("button", { type: "button", className: "ba-btn ba-btn-small ba-btn-danger" }, ["删除"]);
      saveButton.addEventListener("click", async () => {
        try {
          await api.patch(`/api/admin/forums/${forum.id}`, {
            name: nameInput.value.trim(),
            description: descInput.value.trim(),
            sort_order: Number.parseInt(sortInput.value, 10) || 0,
          });
          load();
        } catch (error) {
          window.alert(error instanceof ApiError ? error.message : "保存失败");
        }
      });
      deleteButton.addEventListener("click", async () => {
        const confirmed = await confirmDialog(`确定要删除板块《${forum.name}》吗?板块下仍有主题时会被拒绝。`);
        if (!confirmed) {
          return;
        }
        try {
          await api.delete(`/api/admin/forums/${forum.id}`);
          load();
        } catch (error) {
          window.alert(error instanceof ApiError ? error.message : "删除失败(可能仍有主题)");
        }
      });
      return el("tr", {}, [
        el("td", {}, [forum.slug]),
        el("td", {}, [nameInput]),
        el("td", {}, [descInput]),
        el("td", {}, [sortInput]),
        el("td", { className: "ba-admin-actions-cell" }, [saveButton, deleteButton]),
      ]);
    });
    return tableWrap([
      el("table", { className: "ba-table" }, [
        el("thead", {}, [
          el("tr", {}, [
            el("th", {}, ["slug"]),
            el("th", {}, ["名称"]),
            el("th", {}, ["说明"]),
            el("th", {}, ["排序"]),
            el("th", {}, ["操作"]),
          ]),
        ]),
        el("tbody", {}, rows),
      ]),
    ]);
  }

  load();
}

// ----------------------------------------------------------------- Users --
function initUsersPanel(container) {
  let roleFilter = "";
  let page = 1;

  async function load() {
    clear(container);
    container.append(loadingState("正在加载用户列表..."));
    try {
      const query = roleFilter ? `&role=${roleFilter}` : "";
      const data = await api.get(`/api/admin/users?page=${page}&page_size=${PAGE_SIZE}${query}`);
      clear(container);
      container.append(
        buildToolbar(),
        buildTable(data),
        adminPagination(page, PAGE_SIZE, data.total, (target) => {
          page = target;
          load();
        })
      );
    } catch (error) {
      clear(container);
      container.append(errorState(error instanceof ApiError ? error.message : "加载失败", load));
    }
  }

  function buildToolbar() {
    const select = el(
      "select",
      {
        onChange: (event) => {
          roleFilter = event.target.value;
          page = 1;
          load();
        },
      },
      [
        el("option", { value: "" }, ["全部角色"]),
        el("option", { value: "user" }, ["普通用户"]),
        el("option", { value: "admin" }, ["管理员"]),
      ]
    );
    select.value = roleFilter;
    return el("div", { className: "ba-admin-toolbar" }, [
      el("div", { className: "ba-field" }, [el("label", {}, ["角色筛选"]), select]),
    ]);
  }

  function buildTable(data) {
    if (data.items.length === 0) {
      return emptyState("没有符合条件的用户。");
    }
    const currentUser = getCurrentUser();
    const rows = data.items.map((user) => {
      const isSelf = currentUser && currentUser.id === user.id;
      const roleButton = el(
        "button",
        { type: "button", className: "ba-btn ba-btn-small" },
        [user.role === "admin" ? "降为普通用户" : "提升为管理员"]
      );
      roleButton.disabled = isSelf && user.role === "admin";
      roleButton.addEventListener("click", async () => {
        const nextRole = user.role === "admin" ? "user" : "admin";
        const confirmed = await confirmDialog(
          `确定要将用户「${user.username}」的角色改为${nextRole === "admin" ? "管理员" : "普通用户"}吗?`
        );
        if (!confirmed) {
          return;
        }
        try {
          await withReauth(() => api.patch(`/api/admin/users/${user.id}/role`, { role: nextRole }));
          load();
        } catch (error) {
          window.alert(error instanceof ApiError ? error.message : "操作失败");
        }
      });

      const isBanned = Boolean(user.banned_until && user.banned_until > Math.floor(Date.now() / 1000));
      const banButton = el("button", { type: "button", className: "ba-btn ba-btn-small ba-btn-danger" }, [
        isBanned ? "解除封禁" : "封禁",
      ]);
      banButton.disabled = isSelf;
      banButton.addEventListener("click", async () => {
        let bannedUntil = null;
        if (!isBanned) {
          const days = window.prompt("请输入封禁天数(整数,留空取消):", "7");
          if (!days) {
            return;
          }
          const parsedDays = Number.parseInt(days, 10);
          if (!Number.isFinite(parsedDays) || parsedDays <= 0) {
            window.alert("请输入有效的天数");
            return;
          }
          bannedUntil = Math.floor(Date.now() / 1000) + parsedDays * 86400;
        }
        const confirmed = await confirmDialog(
          isBanned ? `确定要解除用户「${user.username}」的封禁吗?` : `确定要封禁用户「${user.username}」吗?`
        );
        if (!confirmed) {
          return;
        }
        try {
          await withReauth(() => api.patch(`/api/admin/users/${user.id}/ban`, { banned_until: bannedUntil }));
          load();
        } catch (error) {
          window.alert(error instanceof ApiError ? error.message : "操作失败");
        }
      });

      return el("tr", {}, [
        el("td", {}, [String(user.id)]),
        el("td", {}, [user.username]),
        el("td", {}, [user.email || "-"]),
        el("td", {}, [user.role === "admin" ? "管理员" : "普通用户"]),
        el("td", {}, [isBanned ? timeElement(user.banned_until) : "未封禁"]),
        el("td", {}, [timeElement(user.created_at)]),
        el("td", { className: "ba-admin-actions-cell" }, [roleButton, banButton]),
      ]);
    });
    return tableWrap([
      el("table", { className: "ba-table" }, [
        el("thead", {}, [
          el("tr", {}, [
            el("th", {}, ["ID"]),
            el("th", {}, ["用户名"]),
            el("th", {}, ["邮箱"]),
            el("th", {}, ["角色"]),
            el("th", {}, ["封禁至"]),
            el("th", {}, ["注册时间"]),
            el("th", {}, ["操作"]),
          ]),
        ]),
        el("tbody", {}, rows),
      ]),
    ]);
  }

  load();
}

// -------------------------------------------------------- Deleted content --
function initDeletedPanel(container) {
  let kind = "threads";
  let page = 1;

  async function load() {
    clear(container);
    container.append(loadingState("正在加载已删除内容..."));
    try {
      const data = await api.get(`/api/admin/deleted/${kind}?page=${page}&page_size=${PAGE_SIZE}`);
      clear(container);
      container.append(
        buildToolbar(),
        buildTable(data),
        adminPagination(page, PAGE_SIZE, data.total, (target) => {
          page = target;
          load();
        })
      );
    } catch (error) {
      clear(container);
      container.append(errorState(error instanceof ApiError ? error.message : "加载失败", load));
    }
  }

  function buildToolbar() {
    const select = el(
      "select",
      {
        onChange: (event) => {
          kind = event.target.value;
          page = 1;
          load();
        },
      },
      [
        el("option", { value: "threads" }, ["主题"]),
        el("option", { value: "posts" }, ["楼层"]),
        el("option", { value: "sub_posts" }, ["楼中楼"]),
      ]
    );
    select.value = kind;
    return el("div", { className: "ba-admin-toolbar" }, [
      el("div", { className: "ba-field" }, [el("label", {}, ["内容类型"]), select]),
    ]);
  }

  function restoreEndpoint(item) {
    if (kind === "threads") {
      return `/api/admin/threads/${item.id}/delete`;
    }
    if (kind === "posts") {
      return `/api/admin/posts/${item.id}/delete`;
    }
    return `/api/admin/sub_posts/${item.id}/delete`;
  }

  function buildTable(data) {
    if (data.items.length === 0) {
      return emptyState("目前没有已删除的内容。");
    }
    const rows = data.items.map((item) => {
      const restoreButton = el("button", { type: "button", className: "ba-btn ba-btn-small" }, ["恢复"]);
      restoreButton.addEventListener("click", async () => {
        const confirmed = await confirmDialog("确定要恢复这条内容吗?", { danger: false, confirmLabel: "恢复" });
        if (!confirmed) {
          return;
        }
        try {
          await api.patch(restoreEndpoint(item), { is_deleted: false });
          load();
        } catch (error) {
          window.alert(error instanceof ApiError ? error.message : "恢复失败");
        }
      });

      const contextLabel =
        kind === "threads"
          ? `${item.forum.name} · ${item.title}`
          : `《${item.thread_title}》${kind === "posts" ? ` #${item.floor_no}` : "(楼中楼)"}`;
      const deletedBy = item.deleted_by && item.deleted_by.username ? item.deleted_by.username : "作者本人";

      return el("tr", { className: "ba-admin-danger-row" }, [
        el("td", {}, [contextLabel]),
        el("td", {}, [item.author.username]),
        el("td", {}, [item.body_excerpt]),
        el("td", {}, [deletedBy]),
        el("td", {}, [timeElement(item.deleted_at)]),
        el("td", { className: "ba-admin-actions-cell" }, [restoreButton]),
      ]);
    });
    return tableWrap([
      el("table", { className: "ba-table" }, [
        el("thead", {}, [
          el("tr", {}, [
            el("th", {}, ["位置"]),
            el("th", {}, ["作者"]),
            el("th", {}, ["内容摘要"]),
            el("th", {}, ["删除者"]),
            el("th", {}, ["删除时间"]),
            el("th", {}, ["操作"]),
          ]),
        ]),
        el("tbody", {}, rows),
      ]),
    ]);
  }

  load();
}

// -------------------------------------------------------------- Sessions --
function initSessionsPanel(container) {
  let userIdFilter = "";
  let page = 1;

  async function load() {
    clear(container);
    container.append(loadingState("正在加载会话列表..."));
    try {
      const query = userIdFilter ? `&user_id=${encodeURIComponent(userIdFilter)}` : "";
      const data = await api.get(`/api/admin/sessions?page=${page}&page_size=${PAGE_SIZE}${query}`);
      clear(container);
      container.append(
        buildToolbar(),
        buildTable(data),
        adminPagination(page, PAGE_SIZE, data.total, (target) => {
          page = target;
          load();
        })
      );
    } catch (error) {
      clear(container);
      container.append(errorState(error instanceof ApiError ? error.message : "加载失败", load));
    }
  }

  function buildToolbar() {
    const input = el("input", { type: "number", value: userIdFilter, placeholder: "用户ID" });
    const button = el("button", { type: "button", className: "ba-btn ba-btn-small" }, ["筛选"]);
    button.addEventListener("click", () => {
      userIdFilter = input.value.trim();
      page = 1;
      load();
    });
    return el("div", { className: "ba-admin-toolbar" }, [
      el("div", { className: "ba-field" }, [el("label", {}, ["按用户ID筛选"]), input]),
      button,
    ]);
  }

  function buildTable(data) {
    if (data.items.length === 0) {
      return emptyState("没有符合条件的会话。");
    }
    const rows = data.items.map((session) => {
      const isRevoked = Boolean(session.revoked_at);
      const revokeButton = el("button", { type: "button", className: "ba-btn ba-btn-small ba-btn-danger" }, [
        "撤销",
      ]);
      revokeButton.disabled = isRevoked;
      revokeButton.addEventListener("click", async () => {
        const confirmed = await confirmDialog(`确定要撤销用户「${session.username}」的这个会话吗?`);
        if (!confirmed) {
          return;
        }
        try {
          await withReauth(() => api.delete(`/api/admin/sessions/${session.token_hash}`));
          load();
        } catch (error) {
          window.alert(error instanceof ApiError ? error.message : "撤销失败");
        }
      });
      return el("tr", {}, [
        el("td", {}, [session.username]),
        el("td", {}, [timeElement(session.created_at)]),
        el("td", {}, [timeElement(session.expires_at)]),
        el("td", {}, [session.revoked_at ? timeElement(session.revoked_at) : "未撤销"]),
        el("td", {}, [session.ip]),
        el("td", {}, [session.user_agent]),
        el("td", { className: "ba-admin-actions-cell" }, [revokeButton]),
      ]);
    });
    return tableWrap([
      el("table", { className: "ba-table" }, [
        el("thead", {}, [
          el("tr", {}, [
            el("th", {}, ["用户"]),
            el("th", {}, ["创建时间"]),
            el("th", {}, ["过期时间"]),
            el("th", {}, ["撤销时间"]),
            el("th", {}, ["IP"]),
            el("th", {}, ["User-Agent"]),
            el("th", {}, ["操作"]),
          ]),
        ]),
        el("tbody", {}, rows),
      ]),
    ]);
  }

  load();
}

// ----------------------------------------------------------------- Audit --
function initAuditPanel(container) {
  let page = 1;

  async function load() {
    clear(container);
    container.append(loadingState("正在加载审计日志..."));
    try {
      const data = await api.get(`/api/admin/audit_logs?page=${page}&page_size=${PAGE_SIZE}`);
      clear(container);
      container.append(buildTable(data));
      container.append(
        adminPagination(page, PAGE_SIZE, data.total, (target) => {
          page = target;
          load();
        })
      );
    } catch (error) {
      clear(container);
      container.append(errorState(error instanceof ApiError ? error.message : "加载失败", load));
    }
  }

  function buildTable(data) {
    if (data.items.length === 0) {
      return emptyState("目前没有审计日志。");
    }
    const rows = data.items.map((entry) =>
      el("tr", {}, [
        el("td", {}, [timeElement(entry.created_at)]),
        el("td", {}, [entry.admin_id !== null && entry.admin_id !== undefined ? String(entry.admin_id) : "-"]),
        el("td", {}, [entry.action]),
        el("td", {}, [`${entry.target_type}#${entry.target_id}`]),
        el("td", { className: "ba-audit-detail" }, [entry.detail]),
      ])
    );
    return tableWrap([
      el("table", { className: "ba-table" }, [
        el("thead", {}, [
          el("tr", {}, [
            el("th", {}, ["时间"]),
            el("th", {}, ["管理员ID"]),
            el("th", {}, ["动作"]),
            el("th", {}, ["目标"]),
            el("th", {}, ["详情"]),
          ]),
        ]),
        el("tbody", {}, rows),
      ]),
    ]);
  }

  load();
}

const PANEL_INITIALIZERS = {
  forums: initForumsPanel,
  users: initUsersPanel,
  deleted: initDeletedPanel,
  sessions: initSessionsPanel,
  audit: initAuditPanel,
};
const initializedPanels = new Set();

function activateTab(name) {
  for (const tab of document.querySelectorAll("[data-tab]")) {
    const active = tab.dataset.tab === name;
    tab.setAttribute("aria-selected", active ? "true" : "false");
  }
  for (const panel of document.querySelectorAll("[data-panel]")) {
    const active = panel.dataset.panel === name;
    panel.hidden = !active;
    panel.classList.toggle("ba-admin-panel-active", active);
  }
  if (!initializedPanels.has(name)) {
    initializedPanels.add(name);
    PANEL_INITIALIZERS[name](document.querySelector(`[data-panel="${name}"]`));
  }
}

async function init() {
  await loadSession().catch(() => null);
  const user = getCurrentUser();
  if (!user) {
    redirectToLogin("/admin");
    return;
  }
  if (user.role !== "admin") {
    clear(document.querySelector("[data-admin-guard]"));
    document
      .querySelector("[data-admin-guard]")
      .append(errorState("此页面仅管理员可访问。", null));
    return;
  }

  document.querySelector("[data-admin-guard]").hidden = true;
  document.querySelector("[data-admin-root]").hidden = false;

  for (const tab of document.querySelectorAll("[data-tab]")) {
    tab.addEventListener("click", () => activateTab(tab.dataset.tab));
  }
  activateTab("forums");
}

document.addEventListener("DOMContentLoaded", init);
