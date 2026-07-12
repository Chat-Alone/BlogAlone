// Image upload journey: select-a-file path through the Markdown editor,
// server-side validation success, and the inserted Markdown rendering as an
// <img> in the persisted thread body_html.
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const { test, expect } = require("@playwright/test");

const repoRoot = path.resolve(__dirname, "..", "..", "..");
const dbPath = path.join(repoRoot, "blogalone.dev.db");
const promoteScript = path.join(__dirname, "..", "promote-admin.py");

// Minimal valid 1x1 RGBA PNG, base64-encoded (same fixture bytes used by the
// C++ integration test suite's valid_1x1_png() helper).
const PNG_BASE64 =
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAANSURBVBhXY/jPwPAfAAUAAf+mXJtdAAAAAElFTkSuQmCC";

function unique(prefix) {
  return `${prefix}${Date.now().toString(36)}${Math.floor(Math.random() * 1000)}`;
}

test("selecting an image in the editor uploads it and inserts the site-relative URL", async ({ page }) => {
  const username = unique("uploader");
  const password = "correct-horse-battery-staple";
  const adminUsername = unique("uploadadmin");
  const forumSlug = unique("upload-forum-");
  const pngPath = path.join(os.tmpdir(), `blogalone-e2e-${Date.now()}.png`);
  fs.writeFileSync(pngPath, Buffer.from(PNG_BASE64, "base64"));

  try {
    await page.goto("/register");
    await page.fill("#register-username", adminUsername);
    await page.fill("#register-email", `${adminUsername}@example.com`);
    await page.fill("#register-password", password);
    await page.fill("#register-password-confirm", password);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");
    execFileSync("python", [promoteScript, dbPath, adminUsername], { stdio: "inherit" });

    await page.click("[data-session-box] button");
    await expect(page.locator("[data-session-box]")).toContainText("登录");
    await page.goto("/login");
    await page.fill("#login-username", adminUsername);
    await page.fill("#login-password", password);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");

    await page.goto("/admin");
    const toolbar = page.locator('[data-panel="forums"] .ba-admin-toolbar').first();
    await toolbar.locator('input[type="text"]').nth(0).fill(forumSlug);
    await toolbar.locator('input[type="text"]').nth(1).fill("上传测试板块");
    await toolbar.locator('button[type="submit"]').click();
    await expect(page.locator('[data-panel="forums"] table')).toContainText(forumSlug);
    await page.click("[data-session-box] button");
    await expect(page.locator("[data-session-box]")).toContainText("登录");

    await page.goto("/register");
    await page.fill("#register-username", username);
    await page.fill("#register-email", `${username}@example.com`);
    await page.fill("#register-password", password);
    await page.fill("#register-password-confirm", password);
    await page.click("[data-submit-button]");
    await page.waitForURL("**/");

    await page.goto(`/compose?forum=${encodeURIComponent(forumSlug)}`);
    await page.fill("#compose-title", "带图片的主题");
    await page.fill(".ba-editor-textarea", "上传前的正文。");

    const fileInput = page.locator('input[type="file"]');
    await fileInput.setInputFiles(pngPath);
    await expect(page.locator(".ba-upload-status")).toContainText("成功", { timeout: 10_000 });
    await expect(page.locator(".ba-upload-status")).not.toHaveClass(/ba-upload-status-error/);

    const textareaValue = await page.locator(".ba-editor-textarea").inputValue();
    expect(textareaValue).toContain("上传前的正文");
    expect(textareaValue).toMatch(/!\[图片\]\(\/uploads\//);

    await page.click("[data-submit-button]");
    await page.waitForURL(/\/threads\/\d+/);
    await expect(page.locator(".ba-floor-body img").first()).toBeVisible();
    const imgSrc = await page.locator(".ba-floor-body img").first().getAttribute("src");
    expect(imgSrc).toMatch(/^\/uploads\//);

    const imgResponse = await page.request.get(imgSrc);
    expect(imgResponse.status()).toBe(200);
    expect(imgResponse.headers()["content-type"]).toContain("image/png");
  } finally {
    fs.rmSync(pngPath, { force: true });
  }
});
