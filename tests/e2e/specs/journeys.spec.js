// End-to-end journeys covering primary_goal user flows against a real
// running BlogAlone backend + isolated temporary SQLite database.
// Tests share a single browser context (and therefore cookies) on purpose:
// they exercise a continuous session lifecycle rather than isolated pages.
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const { test, expect } = require("@playwright/test");

const dbPath = process.env.BLOGALONE_E2E_DB_PATH;
const promoteScript = path.join(__dirname, "..", "promote-admin.py");
const PNG_BASE64 =
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAANSURBVBhXY/jPwPAfAAUAAf+mXJtdAAAAAElFTkSuQmCC";

if (!dbPath) {
  throw new Error("BLOGALONE_E2E_DB_PATH is required");
}

function unique(prefix) {
  return `${prefix}${Date.now().toString(36)}${Math.floor(Math.random() * 1000)}`;
}

function promoteToAdmin(username) {
  execFileSync("python", [promoteScript, dbPath, username], { stdio: "inherit" });
}

function waitForApiResponse(page, method, pathFragment) {
  return page.waitForResponse((response) => {
    const request = response.request();
    return request.method() === method && response.url().includes(pathFragment) && response.ok();
  });
}

async function logoutThroughHeader(page) {
  await Promise.all([
    page.waitForNavigation({ waitUntil: "load" }),
    page.click("[data-session-box] button"),
  ]);
}

test.describe.serial("public browsing, auth, and content journeys", () => {
  const forumSlug = unique("e2e-forum-");
  const authorUsername = unique("author");
  const authorPassword = "correct-horse-battery-staple";
  const adminUsername = unique("admin");
  const adminPassword = "correct-horse-battery-staple";
  let threadUrl = "";
  let context;
  let page;

  test.beforeAll(async ({ browser }) => {
    context = await browser.newContext();
    page = await context.newPage();
  });

  test.afterAll(async () => {
    await context.close();
  });

  test("home page loads with the forum directory panel", async () => {
    await page.goto("/");
    await expect(page.locator("h1")).toContainText("板块目录");
  });

  test("register creates an account, sets cookies, and redirects home", async () => {
    await page.goto("/register");
    await page.fill("#register-username", authorUsername);
    await page.fill("#register-email", `${authorUsername}@example.com`);
    await page.fill("#register-password", authorPassword);
    await page.fill("#register-password-confirm", authorPassword);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");
    await expect(page.locator("[data-session-box]")).toContainText(authorUsername);
  });

  test("logout returns to visitor chrome, login restores the session", async () => {
    await page.goto("/");
    await expect(page.locator("[data-session-box]")).toContainText(authorUsername);
    await logoutThroughHeader(page);
    await expect(page.locator("[data-session-box]")).toContainText("登录");

    await page.goto("/login");
    await page.fill("#login-username", authorUsername);
    await page.fill("#login-password", authorPassword);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");
    await expect(page.locator("[data-session-box]")).toContainText(authorUsername);
  });

  test("unauthenticated write attempt redirects to login with return_to", async () => {
    await context.clearCookies();
    await page.goto("/compose");
    await page.waitForURL(/\/login\?return_to=/);
    expect(page.url()).toContain("return_to=%2Fcompose");
  });

  test("author logs back in after the return_to redirect check", async () => {
    await page.fill("#login-username", authorUsername);
    await page.fill("#login-password", authorPassword);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/compose");
  });

  test("register an admin account and promote it via the dev DB helper", async () => {
    await context.clearCookies();
    await page.goto("/register");
    await page.fill("#register-username", adminUsername);
    await page.fill("#register-email", `${adminUsername}@example.com`);
    await page.fill("#register-password", adminPassword);
    await page.fill("#register-password-confirm", adminPassword);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");
    promoteToAdmin(adminUsername);
    await logoutThroughHeader(page);
    await expect(page.locator("[data-session-box]")).toContainText("登录");
  });

  test("admin creates a forum from the admin backend", async () => {
    await page.goto("/login");
    await page.fill("#login-username", adminUsername);
    await page.fill("#login-password", adminPassword);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");

    await page.goto("/admin");
    await expect(page.locator('[data-panel="forums"]')).toBeVisible();
    const toolbar = page.locator('[data-panel="forums"] .ba-admin-toolbar').first();
    await toolbar.getByLabel("slug", { exact: true }).fill(forumSlug);
    await toolbar.getByLabel("名称", { exact: true }).fill("E2E 测试板块");
    await toolbar.locator('button[type="submit"]').click();
    await expect(page.locator('[data-panel="forums"] table')).toContainText(forumSlug);

    // Also exercise the users and audit-log tabs while authenticated, and
    // capture a narrow-viewport screenshot of the authenticated admin table
    // views (the dedicated responsive spec only covers the visitor-facing
    // redirect for /admin, since it has no logged-in fixture of its own).
    await page.click('[data-tab="users"]');
    await expect(page.locator('[data-panel="users"] table')).toContainText(adminUsername);
    const authorRow = page.locator('[data-panel="users"] tbody tr').filter({ hasText: authorUsername });
    await authorRow.getByRole("button", { name: "封禁", exact: true }).click();
    const banDialog = page.locator("dialog.ba-dialog");
    await banDialog.getByLabel("封禁时长").selectOption("86400");
    await banDialog.getByRole("button", { name: "确认封禁" }).click();
    const reauthDialog = page.locator("dialog.ba-dialog");
    await reauthDialog.getByLabel("管理员密码").fill(adminPassword);
    await reauthDialog.getByRole("button", { name: "确认身份" }).click();
    await expect(authorRow.getByRole("button", { name: "解除封禁" })).toBeVisible();
    await authorRow.getByRole("button", { name: "解除封禁" }).click();
    await page.locator("dialog.ba-dialog").getByRole("button", { name: "确认" }).click();
    await expect(authorRow.getByRole("button", { name: "封禁", exact: true })).toBeVisible();
    await page.click('[data-tab="audit"]');
    await expect(page.locator('[data-panel="audit"] table')).toContainText("forum.create");
    await page.click('[data-tab="forums"]');

    const previousSize = page.viewportSize();
    await page.setViewportSize({ width: 360, height: 800 });
    const overflow = await page.evaluate(() => ({
      scrollWidth: document.documentElement.scrollWidth,
      clientWidth: document.documentElement.clientWidth,
    }));
    expect(overflow.scrollWidth).toBeLessThanOrEqual(overflow.clientWidth + 1);
    await page.screenshot({
      path: "test-results/screenshots/authenticated-admin-360x800.png",
      fullPage: true,
    });
    if (previousSize) {
      await page.setViewportSize(previousSize);
    }

    await logoutThroughHeader(page);
    await expect(page.locator("[data-session-box]")).toContainText("登录");
  });

  test("author publishes a thread with a Markdown body and draft autosave", async () => {
    await page.goto("/login");
    await page.fill("#login-username", authorUsername);
    await page.fill("#login-password", authorPassword);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");

    await page.goto(`/compose?forum=${encodeURIComponent(forumSlug)}`);
    await expect(page.locator("#compose-forum")).toHaveValue(forumSlug);
    await page.fill("#compose-title", "E2E 测试主题标题");
    await page.fill(".ba-editor-textarea", "**加粗正文** 用于端到端测试。");
    await expect(page.locator(".ba-char-count")).toContainText("/");

    // Draft autosave: reload the compose page and confirm the draft survives.
    await page.waitForTimeout(700);
    await page.reload();
    await expect(page.locator(".ba-editor-textarea")).toHaveValue(/加粗正文/);
    await expect(page.locator(".ba-draft-note")).toContainText("已恢复");

    await page.click("[data-submit-button]");
    await page.waitForURL(/\/threads\/\d+/);
    threadUrl = page.url();
    await expect(page.locator(".ba-thread-detail-title")).toContainText("E2E 测试主题标题");
    await expect(page.locator(".ba-floor-body").first()).toContainText("加粗正文");
  });

  test("thread appears in the forum listing with correct metadata", async () => {
    await page.goto(`/forums/${encodeURIComponent(forumSlug)}`);
    await expect(page.locator(".ba-thread-table")).toContainText("E2E 测试主题标题");
    await expect(page.locator(".ba-thread-table")).toContainText(authorUsername);
  });

  test("floor reply, sub-post reply, edit, and delete-with-confirm all work", async () => {
    await page.goto(threadUrl);
    const replySection = page.locator("section.ba-panel", { hasText: "发表回复" });
    await replySection.locator(".ba-editor-textarea").fill("这是第一条楼层回复。");
    await replySection.getByRole("button", { name: "发表回复" }).click();

    const floor = page.locator(".ba-floor").nth(0);
    await expect(floor.locator(".ba-floor-body")).toContainText("楼层回复");

    await floor.getByRole("button", { name: "回复", exact: true }).click();
    await floor.locator(".ba-editor-textarea").last().fill("楼中楼测试回复");
    await floor.getByRole("button", { name: "发表楼中楼回复" }).click();
    await expect(floor.locator(".ba-sub-post-body")).toContainText("楼中楼测试回复");

    // Edit the floor reply in place. The floor's own edit/delete buttons are
    // the first ones in DOM order (before the nested sub-post's controls).
    await floor.getByRole("button", { name: "编辑" }).first().click();
    await floor.locator(".ba-editor-textarea").first().fill("已编辑的楼层回复内容。");
    await floor.getByRole("button", { name: "保存修改" }).click();
    await expect(page.locator(".ba-floor").nth(0).locator(".ba-floor-body")).toContainText("已编辑的楼层回复内容");

    // Delete requires an explicit confirmation dialog.
    await page.locator(".ba-floor").nth(0).getByRole("button", { name: "删除", exact: true }).first().click();
    const dialog = page.locator("dialog.ba-dialog");
    await expect(dialog).toBeVisible();
    await dialog.getByRole("button", { name: "确认" }).click();
    await expect(dialog).toHaveCount(0);
  });

  test("admin manages roles, sessions, moderation, and deleted content", async () => {
    await logoutThroughHeader(page);
    await page.goto("/login");
    await page.fill("#login-username", adminUsername);
    await page.fill("#login-password", adminPassword);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");

    await page.goto("/admin");
    await page.click('[data-tab="sessions"]');
    const activeAuthorSession = page
      .locator('[data-panel="sessions"] tbody tr')
      .filter({ hasText: authorUsername })
      .filter({ has: page.locator('button:not([disabled])') })
      .first();
    await expect(activeAuthorSession).toBeVisible();
    await activeAuthorSession.getByRole("button", { name: "撤销" }).click();
    await page.locator("dialog.ba-dialog").getByRole("button", { name: "确认" }).click();
    const reauthDialog = page.locator("dialog.ba-dialog");
    await reauthDialog.getByLabel("管理员密码").fill(adminPassword);
    const revokeResponse = waitForApiResponse(page, "DELETE", "/api/admin/sessions/");
    await reauthDialog.getByRole("button", { name: "确认身份" }).click();
    await revokeResponse;
    await expect(
      page
        .locator('[data-panel="sessions"] tbody tr')
        .filter({ hasText: authorUsername })
        .filter({ has: page.locator('button[disabled]') })
        .first()
    ).toBeVisible();

    await page.click('[data-tab="users"]');
    let authorRow = page.locator('[data-panel="users"] tbody tr').filter({ hasText: authorUsername });
    await authorRow.getByRole("button", { name: "提升为管理员" }).click();
    const promoteResponse = waitForApiResponse(page, "PATCH", "/role");
    await page.locator("dialog.ba-dialog").getByRole("button", { name: "确认" }).click();
    await promoteResponse;
    await expect(authorRow.locator("td").nth(3)).toHaveText("管理员");
    await authorRow.getByRole("button", { name: "降为普通用户" }).click();
    const demoteResponse = waitForApiResponse(page, "PATCH", "/role");
    await page.locator("dialog.ba-dialog").getByRole("button", { name: "确认" }).click();
    await demoteResponse;
    authorRow = page.locator('[data-panel="users"] tbody tr').filter({ hasText: authorUsername });
    await expect(authorRow.locator("td").nth(3)).toHaveText("普通用户");

    await page.click('[data-tab="deleted"]');
    await page.getByLabel("内容类型").selectOption("posts");
    const deletedPostRow = page
      .locator('[data-panel="deleted"] tbody tr')
      .filter({ hasText: "E2E 测试主题标题" });
    await expect(deletedPostRow).toBeVisible();
    await deletedPostRow.getByRole("button", { name: "恢复" }).click();
    const restorePostResponse = waitForApiResponse(page, "PATCH", "/api/admin/posts/");
    await page.locator("dialog.ba-dialog").getByRole("button", { name: "恢复" }).click();
    await restorePostResponse;

    await page.goto(threadUrl);
    await expect(page.locator(".ba-floor").first().locator(".ba-floor-body")).toContainText(
      "已编辑的楼层回复内容"
    );
    await page.route("**/api/admin/threads/*/pin", async (route) => {
      await route.fulfill({
        status: 500,
        contentType: "application/json",
        body: '{"error":{"code":"internal_error","message":"temporary failure","request_id":"req_e2e"}}',
      });
    });
    const pinAlertPromise = new Promise((resolve) => {
      page.once("dialog", async (dialog) => {
        const message = dialog.message();
        await dialog.accept();
        resolve(message);
      });
    });
    await page.getByRole("button", { name: "置顶", exact: true }).click();
    expect(await pinAlertPromise).toContain("temporary failure");
    await page.unroute("**/api/admin/threads/*/pin");
    await expect(page.getByRole("button", { name: "置顶", exact: true })).toBeEnabled();
    await page.getByRole("button", { name: "置顶", exact: true }).click();
    await expect(page.locator(".ba-tag-pinned")).toContainText("置顶");

    await page.getByRole("button", { name: "管理员删除" }).first().click();
    await page.locator("dialog.ba-dialog").getByRole("button", { name: "确认" }).click();
    await page.waitForURL(`**/forums/${forumSlug}`);
    await page.goto("/admin");
    await page.click('[data-tab="deleted"]');
    const deletedThreadRow = page
      .locator('[data-panel="deleted"] tbody tr')
      .filter({ hasText: "E2E 测试主题标题" });
    await deletedThreadRow.getByRole("button", { name: "恢复" }).click();
    const restoreThreadResponse = waitForApiResponse(page, "PATCH", "/api/admin/threads/");
    await page.locator("dialog.ba-dialog").getByRole("button", { name: "恢复" }).click();
    await restoreThreadResponse;
    await expect(deletedThreadRow).toHaveCount(0);

    await page.click('[data-tab="audit"]');
    await expect(page.locator('[data-panel="audit"] table')).toContainText("thread.restore");
    await logoutThroughHeader(page);
    await page.goto("/login");
    await page.fill("#login-username", authorUsername);
    await page.fill("#login-password", authorPassword);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");
  });

  test("profile page updates email and requires login when signed out", async () => {
    let meRequests = 0;
    await page.route("**/api/me", async (route) => {
      meRequests += 1;
      if (meRequests === 1) {
        await route.fulfill({ status: 500, contentType: "application/json", body: '{"error":{"code":"internal_error","message":"temporary","request_id":"req_e2e"}}' });
        return;
      }
      await route.continue();
    });
    await page.goto("/profile");
    await expect(page).toHaveURL(/\/profile$/);
    await expect(page.locator("[data-form-error-holder]")).toContainText("登录状态加载失败");
    await page.locator("[data-form-error-holder]").getByRole("button", { name: "重试" }).click();
    await page.unroute("**/api/me");
    await expect(page.locator("[data-profile-form]")).toBeVisible();
    await expect(page.getByLabel("头像")).toHaveAttribute("type", "file");
    await page.getByLabel("头像").setInputFiles({
      name: "avatar.png",
      mimeType: "image/png",
      buffer: Buffer.from(PNG_BASE64, "base64"),
    });
    await expect(page.locator("[data-avatar-status]")).toContainText("上传成功");
    await page.fill("#profile-email", `${authorUsername}-updated@example.com`);
    await page.click("[data-submit-button]");
    await expect(page.locator("[data-form-notice-holder]")).toContainText("已更新");
    await expect(page.locator("[data-avatar-preview]")).toHaveAttribute("src", /^\/uploads\//);

    await context.clearCookies();
    await page.goto("/profile");
    await page.waitForURL(/\/login\?return_to=%2Fprofile/);
  });
});
