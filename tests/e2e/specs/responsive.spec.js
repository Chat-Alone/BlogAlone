// Responsive checks across the four required viewports: no horizontal
// overflow, and a screenshot saved for manual review.
const { test, expect } = require("@playwright/test");

const VIEWPORTS = [
  { name: "desktop-1440x900", width: 1440, height: 900 },
  { name: "desktop-1024x768", width: 1024, height: 768 },
  { name: "mobile-390x844", width: 390, height: 844 },
  { name: "mobile-360x800", width: 360, height: 800 },
];

const PAGES = ["/", "/login", "/register", "/admin"];

for (const viewport of VIEWPORTS) {
  test.describe(`viewport ${viewport.name}`, () => {
    test.use({ viewport: { width: viewport.width, height: viewport.height } });

    for (const path of PAGES) {
      test(`${path || "home"} has no horizontal overflow`, async ({ page }) => {
        await page.goto(path);
        await page.waitForLoadState("networkidle");
        const overflow = await page.evaluate(() => ({
          scrollWidth: document.documentElement.scrollWidth,
          clientWidth: document.documentElement.clientWidth,
        }));
        expect(overflow.scrollWidth).toBeLessThanOrEqual(overflow.clientWidth + 1);
        await page.screenshot({
          path: `test-results/screenshots/${viewport.name}${path.replace(/\//g, "_") || "_home"}.png`,
          fullPage: true,
        });
      });
    }
  });
}
