import assert from "node:assert/strict";
import { mkdir } from "node:fs/promises";
import { join } from "node:path";
import { chromium } from "playwright";

const origin = process.env.ARENA_BROWSER_URL;
if (!origin) throw new Error("Set ARENA_BROWSER_URL to the local Next origin.");
const screenshotDir = process.env.QA_SCREENSHOT_DIR;
if (screenshotDir) await mkdir(screenshotDir, { recursive: true });

const browser = await chromium.launch({
  headless: true,
  ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {})
});
const pageErrors = [];

async function noBodyOverflow(page, label) {
  const size = await page.evaluate(() => ({ viewport: innerWidth, document: document.documentElement.scrollWidth }));
  assert.ok(size.document <= size.viewport + 1, `${label}: body overflows ${size.document} > ${size.viewport}`);
}

try {
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const page = await context.newPage();
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.goto(origin);
  await page.getByText("Local engine connected").waitFor();
  await page.getByRole("combobox", { name: "Bot policy" }).selectOption("passive");
  await page.getByRole("button", { name: "Deal hand" }).click();
  await page.getByRole("button", { name: "Call 0.5 BB" }).waitFor();
  assert.equal(await page.locator('.seat-opponent [role="img"][aria-label="Hidden card"]').count(), 2);
  const handLabel = await page.locator(".table-meta span").last().textContent();

  const exactRaise = page.getByRole("spinbutton", { name: "Exact target in chips" });
  await exactRaise.fill("200.5");
  assert.equal(await page.getByRole("button", { name: /^Raise to / }).isDisabled(), true);
  await exactRaise.fill("200");
  assert.equal(await page.getByRole("button", { name: /^Raise to / }).isEnabled(), true);

  // The opaque session ID plus HttpOnly cookie must restore the same live hand.
  await page.getByRole("link", { name: "Study" }).click();
  await page.getByRole("link", { name: "Play" }).click();
  await page.getByText("Local engine connected").waitFor();
  await page.waitForFunction(expected => document.querySelector(".table-meta span:last-child")?.textContent === expected, handLabel);
  assert.equal(await page.locator(".table-meta span").last().textContent(), handLabel);

  for (let turn = 0; turn < 20 && await page.locator(".terminal-panel").count() === 0; turn++) {
    const prior = await page.locator(".action-history li").count();
    const check = page.getByRole("button", { name: "Check", exact: true });
    const action = await check.isEnabled() ? check : page.getByRole("button", { name: /^Call/ });
    assert.equal(await action.isEnabled(), true);
    await action.click();
    await page.waitForFunction(count => document.querySelectorAll(".action-history li").length > count || !!document.querySelector(".terminal-panel"), prior);
    if (await page.locator(".terminal-panel").count() === 0) {
      assert.equal(await page.locator('.seat-opponent [role="img"][aria-label="Hidden card"]').count(), 2);
    }
  }
  assert.equal(await page.locator(".terminal-panel").count(), 1, "browser hand did not finish");
  const result = await page.locator(".terminal-panel > strong").textContent();
  assert.equal(await page.locator(".terminal-panel > strong").getAttribute("class"), result.startsWith("-") ? "loss" : result.startsWith("+") ? "win" : "tie");
  await page.getByRole("link", { name: "Inspect this hand" }).waitFor();
  assert.equal(await page.locator('.seat-opponent [role="img"][aria-label="Hidden card"]').count(), 0);
  if (screenshotDir) await page.screenshot({ path: join(screenshotDir, "table-1440.png"), fullPage: true });

  await page.getByRole("link", { name: "Inspect this hand" }).click();
  await page.getByRole("heading", { name: "Decision sequence" }).waitFor();
  assert.ok(await page.locator(".replay-ledger li").count() > 0);
  await page.getByRole("button", { name: "AA", exact: true }).focus();
  await page.keyboard.press("ArrowRight");
  assert.equal(await page.getByRole("button", { name: "AKs", exact: true }).getAttribute("aria-pressed"), "true");
  await page.keyboard.press("ArrowDown");
  assert.equal(await page.getByRole("button", { name: "KK", exact: true }).getAttribute("aria-pressed"), "true");

  for (const width of [375, 768, 1024, 1440]) {
    await page.setViewportSize({ width, height: 900 });
    for (const path of ["/", "/study", "/research"]) {
      await page.goto(new URL(path, origin).toString());
      await noBodyOverflow(page, `${path} at ${width}px`);
    }
    if (screenshotDir && (width === 375 || width === 1440)) {
      await page.screenshot({ path: join(screenshotDir, `research-${width}.png`), fullPage: true });
    }
  }
  await context.close();

  const foldContext = await browser.newContext({ viewport: { width: 375, height: 812 } });
  const foldPage = await foldContext.newPage();
  foldPage.on("pageerror", error => pageErrors.push(error.message));
  await foldPage.goto(origin);
  await foldPage.getByText("Local engine connected").waitFor();
  await foldPage.getByRole("combobox", { name: "Bot policy" }).selectOption("passive");
  await foldPage.getByRole("button", { name: "Deal hand" }).click();
  await foldPage.getByRole("button", { name: "Fold", exact: true }).click();
  await foldPage.locator(".terminal-panel").waitFor();
  assert.equal(await foldPage.locator('.seat-opponent [role="img"][aria-label="Hidden card"]').count(), 2);
  await foldPage.getByRole("link", { name: "Inspect this hand" }).waitFor();
  await foldPage.getByRole("link", { name: "Inspect this hand" }).click();
  await foldPage.getByRole("heading", { name: "Decision sequence" }).waitFor();
  assert.equal(await foldPage.getByText("Action probabilities unavailable").count(), 1);
  await noBodyOverflow(foldPage, "fold replay at 375px");
  await foldContext.close();

  assert.deepEqual(pageErrors, [], "browser page errors");
  console.log("Browser QA passed: complete hand, replay, fold secrecy, keyboard matrix, and 375/768/1024/1440px layouts.");
} finally {
  await browser.close();
}
