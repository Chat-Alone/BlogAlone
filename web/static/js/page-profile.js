import { el, clear, errorState, safeReturnTo } from "./dom-utils.js";
import { api, ApiError } from "./api-client.js";
import { loadSession, getCurrentUser, setCurrentUser, redirectToLogin } from "./session-state.js";

function renderError(message) {
  const holder = document.querySelector("[data-form-error-holder]");
  clear(holder);
  if (message) {
    holder.append(el("div", { className: "ba-inline-error", role: "alert" }, [message]));
  }
}

function renderNotice(message) {
  const holder = document.querySelector("[data-form-notice-holder]");
  clear(holder);
  if (message) {
    holder.append(el("div", { className: "ba-inline-notice" }, [message]));
  }
}

async function uploadAvatar(file, statusEl, previewEl) {
  const allowed = new Set(["image/jpeg", "image/png", "image/gif", "image/webp"]);
  if (!allowed.has(file.type)) {
    statusEl.textContent = "仅支持JPEG、PNG、GIF和WebP格式的图片";
    statusEl.classList.add("ba-upload-status-error");
    return null;
  }
  if (file.size > 5 * 1024 * 1024) {
    statusEl.textContent = "图片超过5MB限制,请压缩后重试";
    statusEl.classList.add("ba-upload-status-error");
    return null;
  }
  statusEl.textContent = "正在上传头像...";
  statusEl.classList.remove("ba-upload-status-error");
  const formData = new FormData();
  formData.append("file", file);
  try {
    const result = await api.upload("/api/uploads", formData);
    previewEl.src = result.upload.url;
    statusEl.textContent = "头像上传成功,点击“保存修改”后生效。";
    return result.upload.url;
  } catch (error) {
    statusEl.textContent = error instanceof ApiError ? error.message : "头像上传失败";
    statusEl.classList.add("ba-upload-status-error");
    return null;
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
    redirectToLogin(safeReturnTo("/profile"));
    return;
  }

  document.querySelector("[data-profile-loading]").hidden = true;
  const form = document.querySelector("[data-profile-form]");
  form.hidden = false;
  form.querySelector("#profile-username").value = user.username;
  form.querySelector("#profile-email").value = user.email || "";

  const preview = form.querySelector("[data-avatar-preview]");
  preview.src = user.avatar_url || "";
  preview.hidden = !user.avatar_url;

  let pendingAvatarUrl = user.avatar_url || "";
  const avatarStatus = form.querySelector("[data-avatar-status]");
  form.querySelector("[data-avatar-input]").addEventListener("change", async (event) => {
    const [file] = event.target.files || [];
    if (!file) {
      return;
    }
    const url = await uploadAvatar(file, avatarStatus, preview);
    if (url) {
      pendingAvatarUrl = url;
      preview.hidden = false;
    }
    event.target.value = "";
  });

  const submitButton = document.querySelector("[data-submit-button]");
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    renderError("");
    renderNotice("");
    const email = form.email.value.trim();
    submitButton.disabled = true;
    try {
      const patchBody = {};
      if (email) {
        patchBody.email = email;
      }
      if (pendingAvatarUrl) {
        patchBody.avatar_url = pendingAvatarUrl;
      }
      const data = await api.patch("/api/me", patchBody);
      setCurrentUser(data.user);
      renderNotice("资料已更新。");
    } catch (error) {
      renderError(error instanceof ApiError ? error.message : "更新失败,请重试");
    } finally {
      submitButton.disabled = false;
    }
  });
}

document.addEventListener("DOMContentLoaded", init);
