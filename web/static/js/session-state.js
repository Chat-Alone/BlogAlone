// Session state derived from GET /api/me. Successful and unauthenticated
// results are cached per page; transient failures remain retryable.
import { api, ApiError } from "./api-client.js";

let currentUser = null;
let loaded = false;
let loadPromise = null;
const listeners = new Set();

function notify() {
  for (const listener of listeners) {
    listener(currentUser);
  }
}

export async function loadSession() {
  if (loadPromise) {
    return loadPromise;
  }
  loadPromise = api
    .get("/api/me")
    .then((data) => {
      currentUser = data.user;
      loaded = true;
      notify();
      return currentUser;
    })
    .catch((error) => {
      if (error instanceof ApiError && (error.status === 401 || error.status === 403)) {
        currentUser = null;
        loaded = true;
        notify();
        return null;
      }
      loadPromise = null;
      loaded = false;
      throw error;
    });
  return loadPromise;
}

export function getCurrentUser() {
  return currentUser;
}

export function isLoaded() {
  return loaded;
}

export function onSessionChange(listener) {
  listeners.add(listener);
  if (loaded) {
    listener(currentUser);
  }
  return () => listeners.delete(listener);
}

export async function logout() {
  try {
    await api.post("/api/auth/logout");
  } finally {
    currentUser = null;
    loadPromise = null;
    loaded = true;
    notify();
  }
}

export function setCurrentUser(user) {
  currentUser = user;
  loadPromise = Promise.resolve(user);
  loaded = true;
  notify();
}

export function isAdmin() {
  return Boolean(currentUser && currentUser.role === "admin");
}

// Redirects unauthenticated visitors to /login with a validated return_to.
export function redirectToLogin(returnToPath) {
  const target = `/login?return_to=${encodeURIComponent(returnToPath)}`;
  window.location.assign(target);
}
