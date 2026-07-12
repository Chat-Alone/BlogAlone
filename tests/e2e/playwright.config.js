// Playwright config for BlogAlone browser tests. Dev/test-only tooling:
// it starts the already-built Drogon binary with an isolated temporary
// database and upload directory.
const path = require("node:path");

const repoRoot = path.resolve(__dirname, "..", "..");
const exePath = path.join(repoRoot, "build-msvc", "Debug", "blogalone.exe");
const workspace = process.env.BLOGALONE_E2E_WORKSPACE;
const configPath = process.env.BLOGALONE_E2E_CONFIG_PATH;
const port = Number.parseInt(process.env.BLOGALONE_E2E_PORT || "", 10);

if (!workspace || !configPath || !Number.isInteger(port)) {
  throw new Error("Run Playwright through npm test to create an isolated E2E workspace");
}
const baseURL = `http://127.0.0.1:${port}`;

/** @type {import('@playwright/test').PlaywrightTestConfig} */
module.exports = {
  testDir: "./specs",
  timeout: 30_000,
  expect: { timeout: 8_000 },
  fullyParallel: false,
  workers: 1,
  reporter: [["list"]],
  use: {
    baseURL,
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
  },
  webServer: {
    command: `"${exePath}" --config "${configPath}"`,
    cwd: repoRoot,
    url: `${baseURL}/api/healthz`,
    reuseExistingServer: false,
    timeout: 30_000,
  },
};
