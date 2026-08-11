#!/usr/bin/env node
/*
 * WebUI section-switch performance for the RTLPlayground Web UI.
 *
 * The firmware on this branch serves a single index.html where sections
 * are divs (id="dash", id="port", ...) toggled by nav('<id>') from the
 * sidebar.  Measures:
 *   - cold start: fresh context, cache disabled, login + reload,
 *     load event time and render time (overview content visible)
 *   - warm switches: click each sidebar entry, poll until the panel is
 *     active and its content selector has >= 1 element, 3 passes, median
 *
 * Usage: node perf_webui.js [url] [password]
 */
const { chromium } = require('../webuitest/node_modules/playwright');
const fs = require('fs');
const path = require('path');

const URL = process.argv[2] || 'http://192.168.10.247';
const PASSWORD = process.argv[3] || '1234';

// [panel id, content selector that proves the panel loaded]
const SECTIONS = [
  ['dash', '#info-table tr'],
  ['port', '#speedtable tr'],
  ['vlan', '#vlan-edit-body tr'],
  ['l2', '#l2table tr'],
  ['stat', '#stat-body tr'],
  ['mirror', '#mirror-rx-ports'],
  ['lag', '#lag-container'],
  ['eee', '#eeetable tr'],
  ['bw', '#bwtable tr'],
  ['qos', '#qos-mode'],
  ['storm', '#storm-table tr'],
  ['acl', '#acl-body'],
  ['sfp', '#hexdump'],
  ['sys', '#sys-ip'],
];

const median = arr => arr.slice().sort((a, b) => a - b)[Math.floor(arr.length / 2)];

async function login(page) {
  await page.goto(URL + '/login.html', { waitUntil: 'load', timeout: 20000 });
  await page.fill('#pwd', PASSWORD);
  await Promise.all([
    page.waitForNavigation({ timeout: 15000 }).catch(() => {}),
    page.click('button[type=submit], input[type=submit]'),
  ]);
  await page.waitForTimeout(1500);
}

async function waitFor(sel, maxWait, every) {
  for (let i = 0; i * every < maxWait; i++) {
    if (await sel.count().catch(() => 0) > 0) return true;
    await new Promise(r => setTimeout(r, every));
  }
  return false;
}

(async () => {
  const browser = await chromium.launch();
  const report = { url: URL, date: new Date().toISOString(), sections: {} };

  // Cold start: fresh context, cache disabled via CDP
  const coldCtx = await browser.newContext();
  const p = await coldCtx.newPage();
  await p.goto(URL + '/login.html', { waitUntil: 'load', timeout: 20000 });
  const cdp = await coldCtx.newCDPSession(p);
  await cdp.send('Network.setCacheDisabled', { cacheDisabled: true });
  await p.fill('#pwd', PASSWORD);
  await Promise.all([
    p.waitForNavigation({ timeout: 15000 }).catch(() => {}),
    p.click('button[type=submit], input[type=submit]'),
  ]);
  await p.waitForTimeout(1000);
  let t0 = Date.now();
  await p.reload({ waitUntil: 'load', timeout: 30000 });
  const loadMs = Date.now() - t0;
  const tRenderStart = t0;
  let renderMs = 'timeout';
  for (let i = 0; i < 60; i++) {
    const vis = await p.locator('#dash.active').count();
    const rows = await p.locator('#info-table tr').count().catch(() => 0);
    if (vis > 0 && rows > 0) { renderMs = Date.now() - tRenderStart; break; }
    await p.waitForTimeout(250);
  }
  report.cold = { load_ms: loadMs, render_ms: renderMs };
  console.log(`cold: load=${loadMs}ms render=${renderMs}ms`);
  await coldCtx.close();

  // Warm switches: one logged-in context, 3 passes
  const ctx = await browser.newContext();
  const page = await ctx.newPage();
  await login(page);
  await page.waitForTimeout(2500);

  for (const [name, sel] of SECTIONS) {
    const samples = [];
    for (let pass = 0; pass < 3; pass++) {
      const t = Date.now();
      await page.click('#nav-' + name, { timeout: 5000 }).catch(() => {});
      let done = false;
      for (let i = 0; i < 40; i++) {
        const active = await page.locator('#' + name + '.active').count().catch(() => 0);
        const n = await page.locator(sel).count().catch(() => 0);
        if (active > 0 && n > 0) { done = true; break; }
        await page.waitForTimeout(100);
      }
      samples.push(done ? Date.now() - t : 'timeout');
    }
    const numeric = samples.filter(s => s !== 'timeout');
    report.sections[name] = samples;
    const med = numeric.length ? median(numeric) : 'timeout';
    console.log(`switch ${name.padEnd(6)} ${String(med).padStart(5)}ms ${JSON.stringify(samples)}`);
  }
  await ctx.close();
  await browser.close();

  const d = new Date().toISOString().replace(/[:.]/g, '-');
  const out = path.join(__dirname, 'report', `perf-webui-${d}.json`);
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(report, null, 2));
  console.log('Report written to ' + out);
})().catch(e => { console.error('FATAL:', e.message.split('\n')[0]); process.exit(1); });
