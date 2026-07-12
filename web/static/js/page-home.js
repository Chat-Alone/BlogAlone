import { el, clear, loadingState, emptyState, errorState } from "./dom-utils.js";
import { api, ApiError } from "./api-client.js";

async function renderForums() {
  const body = document.querySelector("[data-forums-body]");
  const status = document.querySelector("[data-forums-status]");
  if (!body) {
    return;
  }

  clear(body);
  body.append(loadingState("正在加载板块列表..."));

  try {
    const data = await api.get("/api/forums");
    clear(body);
    status.textContent = `共 ${data.items.length} 个板块`;

    if (data.items.length === 0) {
      body.append(emptyState("目前还没有任何板块。"));
      return;
    }

    const table = el("table", { className: "ba-forum-list" }, [
      el("thead", {}, [
        el("tr", {}, [
          el("th", { scope: "col" }, ["板块"]),
          el("th", { scope: "col" }, ["操作"]),
        ]),
      ]),
    ]);
    const tbody = el("tbody");
    for (const forum of data.items) {
      tbody.append(
        el("tr", {}, [
          el("td", {}, [
            el("div", { className: "ba-forum-name" }, [
              el("a", { href: `/forums/${encodeURIComponent(forum.slug)}` }, [forum.name]),
            ]),
            el("div", { className: "ba-forum-desc" }, [forum.description || "暂无说明"]),
          ]),
          el("td", { className: "ba-col-secondary" }, [
            el("a", { href: `/forums/${encodeURIComponent(forum.slug)}` }, ["查看主题"]),
            document.createTextNode(" · "),
            el("a", { href: `/compose?forum=${encodeURIComponent(forum.slug)}` }, ["发表新主题"]),
          ]),
        ])
      );
    }
    table.append(tbody);
    body.append(table);
  } catch (error) {
    status.textContent = "加载失败";
    clear(body);
    const message = error instanceof ApiError ? error.message : "板块列表加载失败";
    body.append(errorState(message, renderForums));
  }
}

document.addEventListener("DOMContentLoaded", renderForums);
