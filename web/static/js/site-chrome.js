// Hydrates the shared header session box (login/register vs profile/logout)
// present on every page's static HTML shell.
import { el, clear } from "./dom-utils.js";
import { loadSession, onSessionChange, logout, getCurrentUser } from "./session-state.js";

function currentPath() {
  return window.location.pathname + window.location.search;
}

function renderSessionBox(user) {
  const box = document.querySelector("[data-session-box]");
  if (!box) {
    return;
  }
  clear(box);

  if (!user) {
    box.append(
      el("a", { href: `/login?return_to=${encodeURIComponent(currentPath())}` }, ["登录"]),
      el("a", { href: "/register" }, ["注册"])
    );
    return;
  }

  box.append(
    el("span", { className: "ba-username" }, [user.username]),
    el("a", { href: "/compose" }, ["发帖"]),
    el("a", { href: "/profile" }, ["个人资料"])
  );
  if (user.role === "admin") {
    box.append(el("a", { href: "/admin" }, ["管理后台"]));
  }
  box.append(
    el(
      "button",
      {
        type: "button",
        className: "ba-btn ba-btn-small",
        onClick: async () => {
          await logout().catch(() => {});
          window.location.assign("/");
        },
      },
      ["退出"]
    )
  );
}

export async function initSiteChrome() {
  const skipLink = document.querySelector(".skip-link");
  if (skipLink && !document.getElementById("main")) {
    skipLink.remove();
  }
  onSessionChange(renderSessionBox);
  try {
    await loadSession();
  } catch (error) {
    // Non-auth failure fetching /api/me: keep visitor chrome, page bodies
    // handle their own retry affordance for data they actually need.
    renderSessionBox(getCurrentUser());
  }
}

document.addEventListener("DOMContentLoaded", () => {
  initSiteChrome();
});
