// API client: fetch wrapper with CSRF header injection and structured errors.
// Never persist session/CSRF tokens or passwords to localStorage or logs.

export class ApiError extends Error {
  constructor(status, code, message, requestId) {
    super(message || "request failed");
    this.name = "ApiError";
    this.status = status;
    this.code = code || "internal_error";
    this.requestId = requestId || "";
  }
}

function readCookie(name) {
  const prefix = `${name}=`;
  const entry = document.cookie
    .split(";")
    .map((part) => part.trim())
    .find((part) => part.startsWith(prefix));
  if (!entry) {
    return "";
  }
  return decodeURIComponent(entry.slice(prefix.length));
}

function csrfToken() {
  return readCookie("ba_csrf");
}

const WRITE_METHODS = new Set(["POST", "PATCH", "PUT", "DELETE"]);

async function request(path, options = {}) {
  const method = (options.method || "GET").toUpperCase();
  const headers = { Accept: "application/json", ...(options.headers || {}) };
  let body = options.body;

  if (body !== undefined && !(body instanceof FormData)) {
    headers["Content-Type"] = "application/json";
    body = JSON.stringify(body);
  }
  if (WRITE_METHODS.has(method)) {
    headers["X-CSRF-Token"] = csrfToken();
  }

  let response;
  try {
    response = await fetch(path, {
      method,
      credentials: "same-origin",
      headers,
      body,
    });
  } catch (networkError) {
    throw new ApiError(0, "network_error", "网络请求失败,请检查连接后重试", "");
  }

  if (response.status === 204) {
    return { ok: true, status: 204, data: null, response };
  }

  let payload = null;
  const text = await response.text();
  if (text) {
    try {
      payload = JSON.parse(text);
    } catch (parseError) {
      payload = null;
    }
  }

  if (!response.ok) {
    const error = payload && payload.error ? payload.error : {};
    throw new ApiError(
      response.status,
      error.code || "internal_error",
      error.message || "请求失败",
      error.request_id || ""
    );
  }

  return { ok: true, status: response.status, data: payload, response };
}

export const api = {
  get(path) {
    return request(path, { method: "GET" }).then((result) => result.data);
  },
  post(path, body) {
    return request(path, { method: "POST", body }).then((result) => result.data);
  },
  patch(path, body) {
    return request(path, { method: "PATCH", body }).then((result) => result.data);
  },
  delete(path) {
    return request(path, { method: "DELETE" }).then((result) => result.data);
  },
  // Multipart upload; caller supplies a FormData instance.
  upload(path, formData) {
    return request(path, { method: "POST", body: formData }).then((result) => result.data);
  },
};
