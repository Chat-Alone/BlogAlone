import { el, clear, loadingState, emptyState, errorState, timeElement, buildPagination, queryParam } from "./dom-utils.js";
import { api, ApiError } from "./api-client.js";

function currentSlug() {
  return decodeURIComponent(window.location.pathname.split("/").filter(Boolean).at(-1) || "");
}

function hrefForPage(slug, pageSize) {
  return (page) => `/forums/${encodeURIComponent(slug)}?page=${page}&page_size=${pageSize}`;
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

async function render() {
  const slug = currentSlug();
  const page = Math.max(1, Number.parseInt(queryParam("page", "1"), 10) || 1);
  const pageSize = Math.min(50, Math.max(1, Number.parseInt(queryParam("page_size", "20"), 10) || 20));

  document.querySelector("[data-compose-link]").href = `/compose?forum=${encodeURIComponent(slug)}`;

  const body = document.querySelector("[data-threads-body]");
  const pagination = document.querySelector("[data-threads-pagination]");
  const status = document.querySelector("[data-threads-status]");
  clear(body);
  body.append(loadingState("正在加载主题列表..."));

  try {
    const forums = await api.get("/api/forums");
    const forum = forums.items.find((item) => item.slug === slug);
    if (forum) {
      document.title = `${forum.name} - BlogAlone`;
      document.querySelector("[data-forum-title]").textContent = forum.name;
      document.querySelector("[data-forum-name]").textContent = forum.name;
      document.querySelector("[data-forum-desc]").textContent = forum.description || "";
    } else {
      document.querySelector("[data-forum-title]").textContent = slug;
      document.querySelector("[data-forum-name]").textContent = slug;
    }

    const threads = await api.get(
      `/api/forums/${encodeURIComponent(slug)}/threads?page=${page}&page_size=${pageSize}`
    );
    clear(body);
    clear(pagination);
    status.textContent = `共 ${threads.total} 个主题`;

    if (threads.items.length === 0) {
      body.append(
        emptyState("本板块还没有主题,来发表第一个主题吧。")
      );
      return;
    }

    const table = el("table", { className: "ba-thread-table" }, [
      el("thead", {}, [
        el("tr", {}, [
          el("th", { scope: "col", className: "ba-thread-title-cell" }, ["标题"]),
          el("th", { scope: "col" }, ["作者"]),
          el("th", { scope: "col", className: "ba-col-hide-narrow" }, ["回复"]),
          el("th", { scope: "col", className: "ba-col-hide-narrow" }, ["最后回复"]),
        ]),
      ]),
    ]);
    const tbody = el("tbody");
    for (const thread of threads.items) {
      tbody.append(
        el("tr", {}, [
          el("td", {}, [
            el("div", { className: "ba-thread-title-row" }, [
              ...flagTags(thread),
              el("a", { href: `/threads/${thread.id}` }, [thread.title]),
            ]),
          ]),
          el("td", { className: "ba-col-secondary" }, [thread.author.username]),
          el("td", { className: "ba-col-secondary ba-col-hide-narrow" }, [String(thread.reply_count)]),
          el(
            "td",
            { className: "ba-col-secondary ba-col-hide-narrow" },
            thread.last_reply_at ? [timeElement(thread.last_reply_at)] : ["-"]
          ),
        ])
      );
    }
    table.append(tbody);
    body.append(table);
    pagination.append(buildPagination(page, pageSize, threads.total, hrefForPage(slug, pageSize)));
  } catch (error) {
    status.textContent = "加载失败";
    clear(body);
    const message = error instanceof ApiError ? error.message : "主题列表加载失败";
    body.append(errorState(message, render));
  }
}

document.addEventListener("DOMContentLoaded", render);
