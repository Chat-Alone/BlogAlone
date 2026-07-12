// Small DOM helpers shared across pages. No innerHTML is used here for
// user-provided strings; only textContent and safe attribute assignment.

export function el(tag, attrs = {}, children = []) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === undefined || value === null || value === false) {
      continue;
    }
    if (key === "className") {
      node.className = value;
    } else if (key === "text") {
      node.textContent = value;
    } else if (key.startsWith("on") && typeof value === "function") {
      node.addEventListener(key.slice(2).toLowerCase(), value);
    } else if (key === "dataset") {
      for (const [dataKey, dataValue] of Object.entries(value)) {
        node.dataset[dataKey] = dataValue;
      }
    } else {
      node.setAttribute(key, value);
    }
  }
  for (const child of [].concat(children)) {
    if (child === undefined || child === null || child === false) {
      continue;
    }
    node.append(typeof child === "string" ? document.createTextNode(child) : child);
  }
  return node;
}

export function clear(node) {
  node.replaceChildren();
}

export function loadingState(message = "正在加载...") {
  return el("div", { className: "ba-loading-state", role: "status" }, [message]);
}

export function emptyState(message) {
  return el("div", { className: "ba-empty-state" }, [el("p", {}, [message])]);
}

export function errorState(message, onRetry) {
  const children = [el("p", {}, [message])];
  if (onRetry) {
    children.push(
      el("button", { type: "button", className: "ba-btn ba-btn-small", onClick: onRetry }, ["重试"])
    );
  }
  return el("div", { className: "ba-error-state", role: "alert" }, children);
}

function pad(value) {
  return String(value).padStart(2, "0");
}

// Renders a <time> element carrying the raw Unix timestamp, with a locale
// string as the visible label. Recent timestamps additionally show a short
// relative description, but the absolute value is always present.
export function timeElement(unixSeconds) {
  const date = new Date(unixSeconds * 1000);
  const iso = date.toISOString();
  const label = `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(
    date.getHours()
  )}:${pad(date.getMinutes())}`;
  const time = el("time", { datetime: iso, title: date.toLocaleString() }, [label]);

  const diffSeconds = Math.round(Date.now() / 1000 - unixSeconds);
  const relative = relativeLabel(diffSeconds);
  if (relative) {
    time.append(document.createTextNode(` (${relative})`));
  }
  return time;
}

function relativeLabel(diffSeconds) {
  if (diffSeconds < 0 || diffSeconds > 86400 * 7) {
    return "";
  }
  if (diffSeconds < 60) {
    return "刚刚";
  }
  if (diffSeconds < 3600) {
    return `${Math.floor(diffSeconds / 60)}分钟前`;
  }
  if (diffSeconds < 86400) {
    return `${Math.floor(diffSeconds / 3600)}小时前`;
  }
  return `${Math.floor(diffSeconds / 86400)}天前`;
}

export function buildPagination(currentPage, pageSize, total, hrefForPage) {
  const totalPages = Math.max(1, Math.ceil(total / pageSize));
  const nav = el("nav", { className: "ba-pagination", "aria-label": "分页" });
  if (currentPage > 1) {
    nav.append(el("a", { href: hrefForPage(currentPage - 1) }, ["上一页"]));
  }
  const start = Math.max(1, currentPage - 3);
  const end = Math.min(totalPages, currentPage + 3);
  for (let page = start; page <= end; page += 1) {
    if (page === currentPage) {
      nav.append(el("span", { className: "ba-page-current" }, [String(page)]));
    } else {
      nav.append(el("a", { href: hrefForPage(page) }, [String(page)]));
    }
  }
  if (currentPage < totalPages) {
    nav.append(el("a", { href: hrefForPage(currentPage + 1) }, ["下一页"]));
  }
  nav.append(el("span", { className: "ba-col-secondary" }, [`共 ${total} 条 / ${totalPages} 页`]));
  return nav;
}

// Validates a return_to value is a same-origin relative path (never an
// absolute URL or protocol-relative "//host" string) before use as a
// redirect target.
export function safeReturnTo(value, fallback = "/") {
  if (typeof value !== "string" || value.length === 0) {
    return fallback;
  }
  if (!value.startsWith("/") || value.startsWith("//")) {
    return fallback;
  }
  if (value.includes("\\") || /^\/[a-z]+:/i.test(value)) {
    return fallback;
  }
  return value;
}

export function queryParam(name, fallback = "") {
  const params = new URLSearchParams(window.location.search);
  return params.get(name) ?? fallback;
}
