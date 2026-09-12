#!/usr/bin/env node
/*
 * Mobile-layout check with phone emulation (390x844, touch).
 *
 * Usage:
 *   node mobile.js [base_url] [password]
 *   WUI_URL=... WUI_PASSWORD=... MOB_SHOTS=/tmp node mobile.js
 *
 * Defaults: base_url=http://127.0.0.1:18080  password=1234
 * Screenshots go to MOB_SHOTS (default /tmp) as mob_<section>.png.
 *
 * Read-only: taps through all 14 nav sections (touch input, proving
 * taps work), asserts no page-level horizontal overflow per section,
 * and fails on any pageerror / console error / failed request.
 *
 * Exit code 0 on pass, 1 on failure, 2 on test crash.
 */
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const BASE = process.env.WUI_URL || process.argv[2] || 'http://127.0.0.1:18080';
const PASSWORD = process.env.WUI_PASSWORD || process.argv[3] || '1234';
const SHOTS = process.env.MOB_SHOTS || '/tmp';
const SECTIONS = ['dash', 'port', 'vlan', 'l2', 'stat', 'mirror', 'lag', 'eee',
                  'bw', 'qos', 'storm', 'acl', 'sfp', 'sys'];

(async () => {
  const failures = [];
  const browser = await chromium.launch();
  const ctx = await browser.newContext({
    viewport: { width: 390, height: 844 },
    hasTouch: true,
    isMobile: true,
    deviceScaleFactor: 2,
    userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) ' +
               'AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 ' +
               'Mobile/15E148 Safari/604.1',
  });
  const page = await ctx.newPage();
  page.on('pageerror', e => failures.push('pageerror: ' + e.message));
  page.on('console', m => { if (m.type() === 'error') failures.push('console: ' + m.text()); });
  page.on('requestfailed', r => failures.push('requestfailed: ' + r.url()));

  try {
    fs.mkdirSync(SHOTS, { recursive: true });
  } catch (e) { /* keep going; screenshots will fail loudly below */ }

  // Login (same flow as test.js).
  await page.goto(BASE + '/login.html', { waitUntil: 'networkidle' });
  await page.fill('#pwd', PASSWORD);
  await Promise.all([
    page.waitForURL(url => url.pathname === '/' || url.pathname === '/index.html', { timeout: 15000 }),
    page.tap('button[type=submit]'),
  ]);
  console.log('OK: logged in via tap (' + page.url() + ')');

  // The sidebar must be a horizontal bar at phone width.
  const navDir = await page.evaluate(() => {
    const nav = document.getElementById('nav');
    return nav && window.getComputedStyle(nav).flexDirection;
  });
  if (navDir !== 'row') failures.push('nav is not horizontal: ' + navDir);
  else console.log('OK: nav is horizontal');

  for (const s of SECTIONS) {
    try {
      await page.tap('#nav-' + s, { timeout: 8000 });
      await page.waitForSelector('#' + s + '.active', { timeout: 8000 });
      // Let the panel's data settle (each panel fetches its JSON).
      await page.waitForTimeout(1500);
      const overflow = await page.evaluate(() => ({
        doc: document.documentElement.scrollWidth,
        win: window.innerWidth,
      }));
      if (overflow.doc > overflow.win + 1) {
        failures.push(s + ': page overflows horizontally (scrollWidth ' +
                      overflow.doc + ' > innerWidth ' + overflow.win + ')');
      }
      await page.screenshot({ path: path.join(SHOTS, 'mob_' + s + '.png') });
      console.log('OK: ' + s + ' (scrollWidth ' + overflow.doc + ')');
    } catch (e) {
      failures.push(s + ': ' + e.message.split('\n')[0]);
    }
  }

  await browser.close();
  if (failures.length) {
    console.log('\nFAILURES:');
    failures.forEach(f => console.log('  - ' + f));
    process.exit(1);
  }
  console.log('\nmobile layout: all ' + SECTIONS.length + ' sections pass, shots in ' + SHOTS);
})().catch(e => { console.error('FATAL:', e.message.split('\n')[0]); process.exit(2); });
