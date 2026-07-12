const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const repoRoot = path.resolve(__dirname, "..", "..");
const workspace = fs.mkdtempSync(path.join(os.tmpdir(), "blogalone-e2e-"));
const databasePath = path.join(workspace, "blogalone.db");
const uploadsPath = path.join(workspace, "uploads");
const configPath = path.join(workspace, "config.json");
const port = 20_000 + (process.pid % 20_000);

function configPathFor(value) {
  return value.split(path.sep).join("/");
}

function removeWorkspace() {
  const temporaryRoot = path.resolve(os.tmpdir());
  const resolvedWorkspace = path.resolve(workspace);
  if (
    path.dirname(resolvedWorkspace) !== temporaryRoot ||
    !path.basename(resolvedWorkspace).startsWith("blogalone-e2e-")
  ) {
    throw new Error(`Refusing to remove unexpected E2E workspace: ${resolvedWorkspace}`);
  }
  fs.rmSync(resolvedWorkspace, { recursive: true, force: true });
}

try {
  fs.mkdirSync(uploadsPath);
  const config = JSON.parse(
    fs.readFileSync(path.join(repoRoot, "config", "config.development.json"), "utf8")
  );
  const databaseConfigPath = configPathFor(databasePath);
  config.listeners = [{ address: "127.0.0.1", port, https: false }];
  config.db_clients[0].filename = databaseConfigPath;
  config.custom_config.web_root = configPathFor(path.join(repoRoot, "web"));
  config.custom_config.uploads_root = configPathFor(uploadsPath);
  for (const plugin of config.plugins) {
    if (plugin.name === "blogalone::plugins::DatabaseMigrationPlugin") {
      plugin.config.database_path = databaseConfigPath;
      plugin.config.migrations_dir = configPathFor(path.join(repoRoot, "migrations"));
    }
  }
  fs.writeFileSync(configPath, JSON.stringify(config, null, 2));

  const playwrightCli = path.join(__dirname, "node_modules", "@playwright", "test", "cli.js");
  const result = spawnSync(process.execPath, [playwrightCli, "test"], {
    cwd: __dirname,
    env: {
      ...process.env,
      BLOGALONE_E2E_CONFIG_PATH: configPath,
      BLOGALONE_E2E_DB_PATH: databasePath,
      BLOGALONE_E2E_PORT: String(port),
      BLOGALONE_E2E_WORKSPACE: workspace,
    },
    stdio: "inherit",
  });
  if (result.error) {
    throw result.error;
  }
  process.exitCode = result.status ?? 1;
} finally {
  removeWorkspace();
}
