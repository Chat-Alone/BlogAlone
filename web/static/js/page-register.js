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
  document.querySelector("[data-login-link]").href = `/login?return_to=${encodeURIComponent(returnTarget())}`;

  await loadSession().catch(() => null);
  if (getCurrentUser()) {
    window.location.replace(returnTarget());
    return;
  }

  const form = document.querySelector("[data-register-form]");
  const submitButton = document.querySelector("[data-submit-button]");
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    renderError("");
    const username = form.username.value.trim();
    const email = form.email.value.trim();
    const password = form.password.value;
    const confirm = form.passwordConfirm.value;

    if (username.length < 3 || username.length > 32) {
      renderError("用户名需为3-32个字符");
      return;
    }
    if (password.length < 8 || password.length > 128) {
      renderError("密码需为8-128个字符");
      return;
    }
    if (password !== confirm) {
      renderError("两次输入的密码不一致");
      return;
    }

    submitButton.disabled = true;
    try {
      const data = await api.post("/api/auth/register", {
        username,
        email: email || undefined,
        password,
      });
      setCurrentUser(data.user);
      window.location.assign(returnTarget());
    } catch (error) {
      if (error instanceof ApiError && error.status === 409) {
        renderError(error.code === "email_taken" ? "该邮箱已被注册" : "该用户名已被使用");
      } else if (error instanceof ApiError && error.status === 429) {
        renderError("注册请求过于频繁,请稍后再试");
      } else {
        renderError(error instanceof ApiError ? error.message : "注册失败,请重试");
      }
    } finally {
      submitButton.disabled = false;
    }
  });
}

document.addEventListener("DOMContentLoaded", init);
