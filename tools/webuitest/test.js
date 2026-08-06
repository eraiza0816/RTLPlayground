#!/usr/bin/env node
/*
 * WebUI end-to-end test against the httpd_sim simulator (or a real device).
 *
 * Usage:
 *   node test.js [base_url] [password]
 *   WUI_URL=... WUI_PASSWORD=... node test.js
 *
 * Defaults: base_url=http://127.0.0.1:18080  password=1234
 *
 * Verifies:
 *   - login redirect and authentication
 *   - dashboard port grid order (1..N, physical order)
 *   - navigation across all pages without console/page errors
 *   - no failed HTTP requests
 *
 * Exit code 0 on pass, 1 on failure.
 */
const { chromium } = require('playwright');

const BASE = process.env.WUI_URL || process.argv[2] || 'http://127.0.0.1:18080';
const PASSWORD = process.env.WUI_PASSWORD || process.argv[3] || '1234';
const PAGES = ['dash', 'stat', 'vlan', 'eee', 'port', 'bw', 'qos', 'storm', 'acl', 'mirror', 'lag', 'l2', 'sys', 'sfp'];

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const consoleErrors = [];
  const pageErrors = [];
  const failedRequests = [];
  const failures = [];
  let loggedIn = false;

  page.on('console', msg => {
    if (msg.type() !== 'error') return;
    // Known device quirk: "/" is served unauthenticated (index.html), so main.js
    // fires one /status.json fetch before login which gets 401 and redirects to
    // login.html.  Tolerate 401 console errors that occur before login.
    if (!loggedIn && /401/.test(msg.text())) return;
    consoleErrors.push(msg.text());
  });
  page.on('pageerror', err => pageErrors.push(String(err)));
  page.on('requestfailed', req => failedRequests.push(req.url() + ' -> ' + ((req.failure() || {}).errorText || '?')));
  page.on('response', res => { if (res.status() >= 400) console.log('  HTTP ' + res.status() + ' response: ' + res.url() + ' [page: ' + page.url() + ']'); });
  page.on('framenavigated', f => { if (f === page.mainFrame()) console.log('  navigate: ' + f.url()); });

  // 1. Load root -> expect redirect to login.html
  await page.goto(BASE + '/', { waitUntil: 'networkidle' });
  if (page.url().includes('login.html')) {
    console.log('OK: redirected to login.html');
  } else {
    failures.push('expected redirect to login.html, got ' + page.url());
    console.log('FAIL: redirect to login.html, got ' + page.url());
  }

  // 2. Login
  await page.fill('#pwd', PASSWORD);
  await Promise.all([
    page.waitForURL(url => url.pathname === '/' || url.pathname === '/index.html', { timeout: 10000 }),
    page.click('button[type=submit]')
  ]);
  console.log('OK: logged in (' + page.url() + ')');
  loggedIn = true;

  // 3. Wait for dashboard and verify port grid
  await page.waitForSelector('#port-grid .port', { timeout: 15000 });
  await page.waitForTimeout(1000);
  const portNums = await page.$$eval('#port-grid .port', els => els.map(e => (e.id.match(/^port-(\d+)$/) || [])[1]).filter(Boolean));
  console.log('Dashboard ports: ' + JSON.stringify(portNums));
  const sorted = portNums.slice().sort((a, b) => a - b);
  if (portNums.length > 0 && JSON.stringify(portNums) === JSON.stringify(sorted)) {
    console.log('OK: dashboard port order is physical 1..N');
  } else {
    failures.push('dashboard port order not sorted: ' + JSON.stringify(portNums));
    console.log('FAIL: dashboard port order ' + JSON.stringify(portNums));
  }

  // 4. Navigate every page and check for exceptions / console errors
  for (const pg of PAGES) {
    const errBefore = consoleErrors.length;
    await page.evaluate(id => { try { nav(id); } catch (e) { window.__navErr = String(e); } }, pg);
    await page.waitForTimeout(2500);
    const navErr = await page.evaluate(() => window.__navErr || null);
    if (navErr) {
      failures.push('nav(' + pg + ') threw: ' + navErr);
      console.log('FAIL: nav(' + pg + ') threw: ' + navErr);
    } else if (consoleErrors.length > errBefore) {
      failures.push('console errors on ' + pg + ': ' + consoleErrors.slice(errBefore).join(' | '));
      console.log('FAIL: console errors on ' + pg + ': ' + consoleErrors.slice(errBefore).join(' | '));
    } else {
      console.log('OK: nav(' + pg + ') clean');
    }
  }

  // 5. Firmware update is a tab inside the System page
  await page.evaluate(() => nav('sys'));
  await page.waitForTimeout(1000);
  const hasUpdateTab = await page.evaluate(() => !!document.getElementById('sys-tab-update') && !!document.getElementById('binFile') && !!document.getElementById('flashBtn'));
  if (hasUpdateTab) {
    console.log('OK: firmware update section present in System page');
  } else {
    failures.push('firmware update section missing in System page');
    console.log('FAIL: firmware update section missing in System page');
  }
  const hasNavUpdate = await page.evaluate(() => !!document.getElementById('nav-update'));
  if (!hasNavUpdate) {
    console.log('OK: standalone nav-update item removed');
  } else {
    failures.push('nav-update item still present');
    console.log('FAIL: nav-update item still present');
  }

  // 6. Final summary
  console.log('\n=== Summary ===');
  console.log('Console errors : ' + (consoleErrors.length ? consoleErrors.join('\n  ') : 'none'));
  console.log('Page errors    : ' + (pageErrors.length ? pageErrors.join('\n  ') : 'none'));
  console.log('Failed requests: ' + (failedRequests.length ? failedRequests.join('\n  ') : 'none'));

  const clean = failures.length === 0 && consoleErrors.length === 0 && pageErrors.length === 0 && failedRequests.length === 0;
  console.log('\nRESULT: ' + (clean ? 'PASS' : 'FAIL'));
  await browser.close();
  process.exit(clean ? 0 : 1);
})().catch(e => { console.error('TEST CRASH:', e); process.exit(2); });
