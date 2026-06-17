(() => {
  const csrfToken = () => {
    const prefix = "ba_csrf=";
    const cookie = document.cookie
      .split(";")
      .map((entry) => entry.trim())
      .find((entry) => entry.startsWith(prefix));

    if (!cookie) {
      return "";
    }
    return decodeURIComponent(cookie.slice(prefix.length));
  };

  const requestJson = async (path, options = {}) => {
    const { headers = {}, ...rest } = options;
    const response = await fetch(path, {
      credentials: "same-origin",
      ...rest,
      headers: {
        Accept: "application/json",
        ...(options.body ? { "Content-Type": "application/json" } : {}),
        ...(options.method && options.method !== "GET" ? { "X-CSRF-Token": csrfToken() } : {}),
        ...headers,
      },
    });

    const body = response.status === 204 ? null : await response.json();
    if (!response.ok) {
      const message = body?.error?.message ?? "request failed";
      throw new Error(message);
    }
    return body;
  };

  const getJson = (path) => requestJson(path);
  const postJson = (path, body) => requestJson(path, {
    method: "POST",
    body: JSON.stringify(body),
  });
  const patchJson = (path, body) => requestJson(path, {
    method: "PATCH",
    body: JSON.stringify(body),
  });
  const deleteJson = (path) => requestJson(path, {
    method: "DELETE",
  });

  const setText = (selector, text) => {
    const element = document.querySelector(selector);
    if (element) {
      element.textContent = text;
    }
  };

  const emptyState = (message) => {
    const container = document.createElement("div");
    container.className = "empty-state";
    const paragraph = document.createElement("p");
    paragraph.textContent = message;
    container.append(paragraph);
    return container;
  };

  const userMeta = (author, createdAt) => {
    const date = new Date(createdAt * 1000);
    return `${author.username} · ${date.toLocaleString()}`;
  };

  const renderHome = async () => {
    const forumsList = document.querySelector("[data-forums-list]");
    const threadsList = document.querySelector("[data-threads-list]");
    if (!forumsList || !threadsList) {
      return;
    }

    try {
      const forums = await getJson("/api/forums");
      forumsList.replaceChildren();
      setText("[data-forums-status]", `${forums.items.length}`);

      if (forums.items.length === 0) {
        forumsList.append(emptyState("No forums yet."));
        threadsList.replaceChildren(emptyState("No threads yet."));
        setText("[data-threads-status]", "Empty");
        return;
      }

      const loadThreads = async (forum) => {
        setText("[data-current-forum]", forum.name);
        setText("[data-threads-status]", "Loading");
        threadsList.replaceChildren(emptyState("Loading threads."));

        const page = await getJson(`/api/forums/${encodeURIComponent(forum.slug)}/threads`);
        threadsList.replaceChildren();
        setText("[data-threads-status]", `${page.total}`);
        if (page.items.length === 0) {
          threadsList.append(emptyState("No threads yet."));
          return;
        }

        for (const thread of page.items) {
          const link = document.createElement("a");
          link.className = "thread-row";
          link.href = `/threads/${thread.id}`;

          const title = document.createElement("strong");
          title.textContent = thread.title;
          const meta = document.createElement("span");
          meta.className = "thread-meta";
          meta.textContent = `${thread.author.username} · ${thread.reply_count} replies`;

          link.append(title, meta);
          threadsList.append(link);
        }
      };

      for (const forum of forums.items) {
        const button = document.createElement("button");
        button.className = "forum-row";
        button.type = "button";

        const title = document.createElement("strong");
        title.textContent = forum.name;
        const description = document.createElement("span");
        description.textContent = forum.description || forum.slug;

        button.append(title, description);
        button.addEventListener("click", () => {
          loadThreads(forum).catch((error) => {
            threadsList.replaceChildren(emptyState(error.message));
            setText("[data-threads-status]", "Error");
          });
        });
        forumsList.append(button);
      }

      await loadThreads(forums.items[0]);
    } catch (error) {
      forumsList.replaceChildren(emptyState(error.message));
      setText("[data-forums-status]", "Error");
    }
  };

  const renderThread = async () => {
    const body = document.querySelector("[data-thread-body]");
    const postsList = document.querySelector("[data-posts-list]");
    if (!body || !postsList) {
      return;
    }

    const threadId = window.location.pathname.split("/").filter(Boolean).at(-1);
    try {
      const detail = await getJson(`/api/threads/${encodeURIComponent(threadId)}`);
      document.title = `${detail.thread.title} - BlogAlone`;
      setText("#thread-title", detail.thread.title);
      setText("[data-thread-status]", `${detail.thread.reply_count} replies`);
      setText("[data-posts-count]", `${detail.posts.total}`);
      body.innerHTML = detail.thread.body_html;

      postsList.replaceChildren();
      if (detail.posts.items.length === 0) {
        postsList.append(emptyState("No replies yet."));
        return;
      }

      for (const item of detail.posts.items) {
        const article = document.createElement("article");
        article.className = "post";

        const meta = document.createElement("p");
        meta.className = "thread-meta";
        meta.textContent = `#${item.floor_no} · ${userMeta(item.author, item.created_at)}`;

        const postBody = document.createElement("div");
        postBody.className = "rich-body";
        postBody.innerHTML = item.body_html;

        const replies = document.createElement("div");
        replies.className = "sub-post-list";
        for (const subPost of item.sub_posts) {
          const reply = document.createElement("div");
          reply.className = "sub-post";
          const replyMeta = document.createElement("p");
          replyMeta.className = "thread-meta";
          replyMeta.textContent = userMeta(subPost.author, subPost.created_at);
          const replyBody = document.createElement("div");
          replyBody.className = "rich-body";
          replyBody.innerHTML = subPost.body_html;
          reply.append(replyMeta, replyBody);
          replies.append(reply);
        }

        article.append(meta, postBody, replies);
        postsList.append(article);
      }
    } catch (error) {
      setText("#thread-title", "Thread not found");
      setText("[data-thread-status]", "Error");
      body.replaceChildren(emptyState(error.message));
      postsList.replaceChildren();
    }
  };

  document.addEventListener("DOMContentLoaded", () => {
    renderHome();
    renderThread();
  });

  window.BlogAlone = Object.freeze({
    deleteJson,
    getJson,
    patchJson,
    postJson,
  });
})();
