// Session state derived from GET /api/me. Fetched once per page load;
// a 401 clears in-memory state and does not retry automatically.
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
      return currentUser;
    })
    .catch((error) => {
      if (error instanceof ApiError && (error.status === 401 || error.status === 403)) {
        currentUser = null;
        return null;
      }
      // Non-auth failures (network, 5xx) also leave the visitor state, but
      // are surfaced to the caller so a page can show a retry affordance.
      currentUser = null;
      throw error;
    })
    .finally(() => {
      loaded = true;
      notify();
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
    notify();
  }
}

export function setCurrentUser(user) {
  currentUser = user;
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
