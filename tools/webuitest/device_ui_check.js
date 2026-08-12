#!/usr/bin/env node
/*
 * Real-device WebUI check for the audit D-group fixes.
 *
 * Runs against a real switch (the simulator does not embed the same
 * assets) and verifies:
 *   - login works and the dashboard renders without JS errors (D1/D9)
 *   - all 14 sidebar sections switch (D9 i18n keys do not break nav)
 *   - the System tab has the Logout button (B5) and the VLAN table has
 *     a populated PVID column (D5)
 *   - the sidebar is keyboard-focusable (D10)
 *   - at 480 px the sidebar becomes a horizontal bar (D4)
 *
 * Usage: node device_ui_check.js <url> [password]
 *        (url e.g. http://192.168.10.247, password default 1234)
 */
const { chromium } = require('../webuitest/node_modules/playwright');

const URL = process.argv[2] || 'http://192.168.10.247';
const PASSWORD = process.argv[3] || '1234';

const SECTIONS = ['dash', 'port', 'vlan', 'l2', 'stat', 'mirror', 'lag', 'eee',
                  'bw', 'qos', 'storm', 'acl', 'sfp', 'sys'];

const results = [];
function check(name, ok, detail) {
  results.push([name, !!ok, detail || '']);
  console.log((ok ? 'PASS' : 'FAIL') + ' ' + name.padEnd(36) + ' ' + (detail || ''));
}

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext();
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push('pageerror: ' + e.message));
  page.on('console', m => { if (m.type() === 'error') errors.push('console: ' + m.text()); });

  await page.goto(URL + '/login.html', { waitUntil: 'load', timeout: 30000 });
  await page.fill('#pwd', PASSWORD);
  await Promise.all([
    page.waitForNavigation({ timeout: 15000 }).catch(() => {}),
    page.click('button[type=submit]'),
  ]);
  await page.waitForTimeout(4000);

  check('login -> dashboard renders', await page.locator('#info-table tr').count() > 0);
  check('nav() defined (main.js executed)', await page.evaluate(() => typeof nav === 'function'));

  // D9: every section switches without throwing
  let allOk = true;
  for (const s of SECTIONS) {
    try {
      await page.click('#nav-' + s, { timeout: 5000 });
      await page.waitForTimeout(600);
      const active = await page.locator('#' + s + '.active').count();
      if (!active) allOk = false;
    } catch (e) {
      allOk = false;
    }
  }
  check('all 14 sections switch (D9)', allOk);

  // D5: VLAN table PVID column shows something for VLAN 1 (all ports PVID 1).
  // The table body is only filled by refreshVlanViews(); call it directly
  // (read-only: it just fetches /vlanlist).
  await page.click('#nav-vlan');
  await page.waitForTimeout(1000);
  await page.evaluate(() => { if (typeof refreshVlanViews === 'function') refreshVlanViews(); });
  await page.waitForTimeout(2500);
  const pvidCell = await page.locator('#vlanTableBody tr').first().locator('td').nth(5).textContent().catch(() => '');
  check('VLAN table PVID column populated (D5)', pvidCell && pvidCell.trim() !== '-',
        'pvid cell = ' + JSON.stringify(pvidCell));

  // B5: System tab has a Logout button
  await page.click('#nav-sys');
  await page.waitForTimeout(1000);
  const hasLogout = await page.locator('button:has-text("Logout")').count();
  check('System tab has Logout button (B5)', hasLogout > 0);

  // D10: sidebar items are keyboard-focusable and Enter navigates
  const focusable = await page.locator('#nav li[tabindex]').count();
  check('sidebar items keyboard-focusable (D10)', focusable === SECTIONS.length,
        focusable + ' items');
  await page.locator('#nav-stat').focus();
  await page.keyboard.press('Enter');
  await page.waitForTimeout(800);
  check('Enter on nav item switches section (D10)',
        await page.locator('#stat.active').count() > 0);

  // D4: at 480px the sidebar is a horizontal bar (column body)
  await page.setViewportSize({ width: 420, height: 800 });
  await page.waitForTimeout(500);
  const layout = await page.evaluate(() => {
    const nav = document.getElementById('nav');
    const body = document.body;
    const navStyle = window.getComputedStyle(nav);
    const bodyStyle = window.getComputedStyle(body);
    return { navWidth: navStyle.width, navDir: navStyle.flexDirection,
             bodyDir: bodyStyle.flexDirection };
  });
  check('480px layout stacks (D4)', layout.bodyDir === 'column' &&
        (layout.navDir === 'row' || parseFloat(layout.navWidth) > 300),
        JSON.stringify(layout));

  check('no JS errors on the whole tour', errors.length === 0, errors.slice(0, 5).join('; '));

  await browser.close();
  const failed = results.filter(r => !r[1]).length;
  console.log();
  console.log(results.length - failed + ' passed, ' + failed + ' failed');
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error('FATAL:', e.message.split('\n')[0]); process.exit(2); });
