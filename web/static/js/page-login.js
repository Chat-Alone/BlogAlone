import { el, clear, safeReturnTo, queryParam } from "./dom-utils.js";
import { api, ApiError } from "./api-client.js";
import { setCurrentUser, getCurrentUser, loadSession } from "./session-state.js";

function renderError(message) {
  const holder = document.querySelector("[data-form-error-holder]");
  clear(holder);
  if (message) {
    holder.append(el("div", { className: "ba-inline-error", role: "alert" }, [message]));
  }
}

function returnTarget() {
  return safeReturnTo(queryParam("return_to", "/"), "/");
}

async function init() {
  document.querySelector("[data-register-link]").href =
    `/register?return_to=${encodeURIComponent(returnTarget())}`;

  await loadSession().catch(() => null);
  if (getCurrentUser()) {
    window.location.replace(returnTarget());
    return;
  }

  const form = document.querySelector("[data-login-form]");
  const submitButton = document.querySelector("[data-submit-button]");
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    renderError("");
    const username = form.username.value.trim();
    const password = form.password.value;
    if (!username || !password) {
      renderError("请输入用户名和密码");
      return;
    }
    submitButton.disabled = true;
    try {
      const data = await api.post("/api/auth/login", { username, password });
      setCurrentUser(data.user);
      window.location.assign(returnTarget());
    } catch (error) {
      if (error instanceof ApiError && error.status === 429) {
        renderError("登录尝试过于频繁,请稍后再试");
      } else if (error instanceof ApiError && error.status === 403) {
        renderError("该账号已被封禁");
      } else {
        renderError(error instanceof ApiError ? "用户名或密码错误" : "登录失败,请重试");
      }
    } finally {
      submitButton.disabled = false;
    }
  });
}

document.addEventListener("DOMContentLoaded", init);
