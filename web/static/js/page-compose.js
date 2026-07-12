import { el, clear, errorState, safeReturnTo, queryParam } from "./dom-utils.js";
import { api, ApiError } from "./api-client.js";
import { loadSession, getCurrentUser, redirectToLogin } from "./session-state.js";
import { MarkdownEditor } from "./editor.js";

const TITLE_MAX = 80;
const BODY_MAX = 20000;

function renderError(message) {
  const holder = document.querySelector("[data-form-error-holder]");
  clear(holder);
  if (message) {
    holder.append(el("div", { className: "ba-inline-error", role: "alert" }, [message]));
  }
}

function readDraftTitle(titleDraftKey) {
  try {
    return window.localStorage.getItem(titleDraftKey) || "";
  } catch (storageError) {
    return "";
  }
}

function writeDraftTitle(titleDraftKey, value) {
  try {
    if (value) {
      window.localStorage.setItem(titleDraftKey, value);
    } else {
      window.localStorage.removeItem(titleDraftKey);
    }
  } catch (storageError) {
    // Autosave is best-effort; ignore storage failures (quota, private mode).
  }
}

async function init() {
  try {
    await loadSession();
  } catch (error) {
    const holder = document.querySelector("[data-form-error-holder]");
    clear(holder);
    holder.append(errorState("登录状态加载失败,请重试。", init));
    return;
  }
  const user = getCurrentUser();
  if (!user) {
    redirectToLogin(safeReturnTo(window.location.pathname + window.location.search));
    return;
  }

  const form = document.querySelector("[data-compose-form]");
  const forumSelect = form.querySelector("#compose-forum");
  const preselected = queryParam("forum", "");

  try {
    const forums = await api.get("/api/forums");
    if (forums.items.length === 0) {
      renderError("目前还没有任何板块,无法发表主题。");
      form.hidden = true;
      return;
    }
    for (const forum of forums.items) {
      const option = el("option", { value: forum.slug }, [forum.name]);
      forumSelect.append(option);
    }
    if (preselected && forums.items.some((forum) => forum.slug === preselected)) {
      forumSelect.value = preselected;
    }
  } catch (error) {
    renderError("板块列表加载失败,请刷新重试。");
    return;
  }

  const draftKeyFor = (forumSlug) => `ba:draft:${user.id}:compose:${forumSlug || "default"}`;

  const editor = new MarkdownEditor({
    mountPoint: form.querySelector("[data-editor-mount]"),
    maxLength: BODY_MAX,
    draftKey: draftKeyFor(forumSelect.value),
    textareaId: "compose-body",
    textareaLabel: "正文(1-20000字符,支持Markdown)",
    placeholder: "支持Markdown格式,可拖拽、粘贴或选择图片",
  });
  document.getElementById("compose-body-label").htmlFor = "compose-body";

  // Title draft shares the same per-user/per-forum key as the body draft so
  // reloading the compose page restores both fields together.
  let titleDraftKey = `${draftKeyFor(forumSelect.value)}:title`;
  const restoredTitle = readDraftTitle(titleDraftKey);
  if (restoredTitle) {
    form.title.value = restoredTitle;
  }
  form.title.addEventListener("input", () => writeDraftTitle(titleDraftKey, form.title.value));

  // The draft keys embed the selected forum, so switching forums must
  // re-scope both drafts instead of silently saving new content under the
  // key of whichever forum was selected when the page loaded.
  forumSelect.addEventListener("change", () => {
    editor.setDraftKey(draftKeyFor(forumSelect.value));
    titleDraftKey = `${draftKeyFor(forumSelect.value)}:title`;
    form.title.value = readDraftTitle(titleDraftKey);
  });

  const submitButton = document.querySelector("[data-submit-button]");
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    renderError("");
    const title = form.title.value.trim();
    const body = editor.getValue().trim();

    if (!title || [...title].length > TITLE_MAX) {
      renderError("标题需为1-80个字符");
      return;
    }
    if (!body || [...body].length > BODY_MAX) {
      renderError("正文需为1-20000个字符");
      return;
    }

    submitButton.disabled = true;
    try {
      const data = await api.post("/api/threads", {
        forum_slug: forumSelect.value,
        title,
        body_md: body,
      });
      editor.clearDraft();
      writeDraftTitle(titleDraftKey, "");
      window.location.assign(`/threads/${data.thread.id}`);
    } catch (error) {
      renderError(error instanceof ApiError ? error.message : "发表失败,请重试");
    } finally {
      submitButton.disabled = false;
    }
  });
}

document.addEventListener("DOMContentLoaded", init);
