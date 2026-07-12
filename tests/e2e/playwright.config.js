// Playwright config for BlogAlone browser tests. Dev/test-only tooling:
// it starts the already-built Drogon binary against the repo-root dev
// config and never touches the production build pipeline.
const path = require("node:path");

const repoRoot = path.resolve(__dirname, "..", "..");
const exePath = path.join(repoRoot, "build-msvc", "Debug", "blogalone.exe");

/** @type {import('@playwright/test').PlaywrightTestConfig} */
module.exports = {
  testDir: "./specs",
  timeout: 30_000,
  expect: { timeout: 8_000 },
  fullyParallel: false,
  workers: 1,
  reporter: [["list"]],
  use: {
    baseURL: "http://127.0.0.1:8080",
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
  },
  webServer: {
    command: `"${exePath}" --config config/config.windows.json`,
    cwd: repoRoot,
    url: "http://127.0.0.1:8080/api/healthz",
    reuseExistingServer: true,
    timeout: 30_000,
  },
};
