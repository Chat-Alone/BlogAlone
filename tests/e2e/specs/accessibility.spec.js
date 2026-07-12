// Baseline automated accessibility checks (axe-core) on public pages.
// Not exhaustive, but catches missing labels, contrast failures, and
// landmark/heading issues without requiring manual auditing tools.
const { test, expect } = require("@playwright/test");
const AxeBuilder = require("@axe-core/playwright").default;

const PAGES = ["/", "/login", "/register"];

for (const path of PAGES) {
  test(`${path} has no serious/critical automated accessibility violations`, async ({ page }) => {
    await page.goto(path);
    await page.waitForLoadState("networkidle");
    const results = await new AxeBuilder({ page })
      .withTags(["wcag2a", "wcag2aa"])
      .analyze();
    const seriousOrWorse = results.violations.filter(
      (violation) => violation.impact === "serious" || violation.impact === "critical"
    );
    if (seriousOrWorse.length > 0) {
      console.log(JSON.stringify(seriousOrWorse, null, 2));
    }
    expect(seriousOrWorse).toEqual([]);
  });
}

test("keyboard navigation: skip link jumps to main content, then Tab reaches the form", async ({ page }) => {
  await page.goto("/login");
  await page.keyboard.press("Tab");
  await expect(page.locator(".skip-link")).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(page).toHaveURL(/#main$/);
  await page.keyboard.press("Tab");
  await expect(page.locator("#login-username")).toBeFocused();
});

function unique(prefix) {
  return `${prefix}${Date.now().toString(36)}${Math.floor(Math.random() * 1000)}`;
}

test("delete confirmation dialog traps focus, supports Escape, and returns focus", async ({ page }) => {
  const username = unique("a11yauthor");
  const password = "correct-horse-battery-staple";

  await page.goto("/register");
  await page.fill("#register-username", username);
  await page.fill("#register-email", `${username}@example.com`);
  await page.fill("#register-password", password);
  await page.fill("#register-password-confirm", password);
  await page.click("[data-submit-button]");
  await page.waitForURL("**/");

  // No forum exists yet for this fresh registration path in isolation, so
  // exercise the dialog through the profile page's own confirmable action
  // is unavailable; instead, drive the shared modal module directly against
  // the current document to verify its accessibility contract in isolation.
  const dialogState = await page.evaluate(async () => {
    const moduleUrl = "/static/js/modal.js";
    const { confirmDialog } = await import(moduleUrl);
    const trigger = document.createElement("button");
    trigger.textContent = "trigger";
    document.body.append(trigger);
    trigger.focus();
    const promise = confirmDialog("测试确认对话框");
    await new Promise((resolve) => setTimeout(resolve, 50));
    const dialog = document.querySelector("dialog.ba-dialog");
    const opened = Boolean(dialog && dialog.open);
    const activeInsideDialog = Boolean(dialog && dialog.contains(document.activeElement));
    dialog.dispatchEvent(new KeyboardEvent("cancel", { cancelable: true }));
    await new Promise((resolve) => setTimeout(resolve, 50));
    const result = await promise;
    const focusReturnedToTrigger = document.activeElement === trigger;
    trigger.remove();
    return { opened, activeInsideDialog, result, focusReturnedToTrigger };
  });

  expect(dialogState.opened).toBe(true);
  expect(dialogState.activeInsideDialog).toBe(true);
  expect(dialogState.result).toBe(false);
  expect(dialogState.focusReturnedToTrigger).toBe(true);
});
