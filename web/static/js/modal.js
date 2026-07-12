// Minimal dependency-free modal built on the native <dialog> element.
// Handles focus entry, focus return, Escape (native), and background scroll lock.
import { el } from "./dom-utils.js";

function focusableElements(container) {
  return Array.from(
    container.querySelectorAll(
      'a[href], button:not([disabled]), textarea:not([disabled]), input:not([disabled]), select:not([disabled]), [tabindex]:not([tabindex="-1"])'
    )
  ).filter((node) => node.offsetParent !== null || node === document.activeElement);
}

export function openDialog({ titleText, bodyNode, actions = [], labelledBy }) {
  const previousFocus = document.activeElement;
  const titleId = labelledBy || `ba-dialog-title-${Math.random().toString(36).slice(2)}`;

  const closeButton = el(
    "button",
    {
      type: "button",
      className: "ba-dialog-close",
      "aria-label": "关闭",
      onClick: () => close(),
    },
    ["×"]
  );

  const actionsRow = el(
    "div",
    { className: "ba-dialog-actions" },
    actions.map((action) =>
      el(
        "button",
        {
          type: "button",
          className: action.className || "ba-btn",
          onClick: () => action.onClick(close),
        },
        [action.label]
      )
    )
  );

  const dialog = el("dialog", { className: "ba-dialog", "aria-labelledby": titleId }, [
    el("div", { className: "ba-dialog-title" }, [
      el("h2", { id: titleId }, [titleText]),
      closeButton,
    ]),
    el("div", { className: "ba-dialog-body" }, [bodyNode]),
    actionsRow,
  ]);

  document.body.append(dialog);
  document.body.classList.add("ba-modal-open-lock");

  function onKeydown(event) {
    if (event.key !== "Tab") {
      return;
    }
    const focusables = focusableElements(dialog);
    if (focusables.length === 0) {
      return;
    }
    const first = focusables[0];
    const last = focusables[focusables.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    }
  }

  function onCancel(event) {
    event.preventDefault();
    close();
  }

  function close() {
    dialog.removeEventListener("keydown", onKeydown);
    dialog.removeEventListener("cancel", onCancel);
    dialog.close();
  }

  dialog.addEventListener("keydown", onKeydown);
  dialog.addEventListener("cancel", onCancel);
  dialog.addEventListener("close", () => {
    document.body.classList.remove("ba-modal-open-lock");
    dialog.remove();
    if (previousFocus && typeof previousFocus.focus === "function") {
      previousFocus.focus();
    }
  });

  dialog.showModal();
  const initialFocusable = focusableElements(dialog)[0];
  if (initialFocusable) {
    initialFocusable.focus();
  }

  return {
    dialog,
    close,
  };
}

export function confirmDialog(message, { confirmLabel = "确认", cancelLabel = "取消", danger = true } = {}) {
  return new Promise((resolve) => {
    let resolved = false;
    const finish = (value) => {
      if (!resolved) {
        resolved = true;
        resolve(value);
      }
    };
    const body = el("p", { className: danger ? "ba-dialog-danger-text" : "" }, [message]);
    const handle = openDialog({
      titleText: "请确认操作",
      bodyNode: body,
      actions: [
        {
          label: cancelLabel,
          className: "ba-btn",
          onClick: (close) => {
            close();
            finish(false);
          },
        },
        {
          label: confirmLabel,
          className: danger ? "ba-btn ba-btn-danger" : "ba-btn ba-btn-primary",
          onClick: (close) => {
            close();
            finish(true);
          },
        },
      ],
    });
    handle.dialog.addEventListener("close", () => finish(false));
  });
}

// Prompts for the current admin's password and calls POST /api/admin/reauth,
// returning true once confirmed. Used before retrying admin_reauth_required actions.
export function passwordPromptDialog(message) {
  return new Promise((resolve) => {
    let resolved = false;
    const finish = (value) => {
      if (!resolved) {
        resolved = true;
        resolve(value);
      }
    };
    const input = el("input", { type: "password", id: "ba-reauth-password", autocomplete: "current-password" });
    const errorBox = el("p", { className: "ba-field-error", role: "alert" });
    const body = el("div", {}, [
      el("p", {}, [message]),
      el("div", { className: "ba-field" }, [
        el("label", { for: "ba-reauth-password" }, ["管理员密码"]),
        input,
      ]),
      errorBox,
    ]);
    const handle = openDialog({
      titleText: "需要二次确认",
      bodyNode: body,
      actions: [
        {
          label: "取消",
          className: "ba-btn",
          onClick: (close) => {
            close();
            finish(null);
          },
        },
        {
          label: "确认身份",
          className: "ba-btn ba-btn-primary",
          onClick: (close) => {
            if (!input.value) {
              errorBox.textContent = "请输入密码";
              return;
            }
            close();
            finish(input.value);
          },
        },
      ],
    });
    handle.dialog.addEventListener("close", () => finish(null));
  });
}
