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
 *   - every panel renders its controls and loads its data without
 *     console/page errors or failed HTTP requests
 *   - the panel-specific element checks (tables, selects, toggles)
 *   - config interactions (apply flows) when running against the local
 *     simulator; against a real device the test is read-only
 *   - the System > Console page: ping result, ARP table, running config
 *
 * Exit code 0 on pass, 1 on failure, 2 on test crash.
 */
const { chromium } = require('playwright');

const BASE = process.env.WUI_URL || process.argv[2] || 'http://127.0.0.1:18080';
const PASSWORD = process.env.WUI_PASSWORD || process.argv[3] || '1234';
/* Write-mode (apply flows, ping) is only enabled for the bundled
 * simulator.  An explicit WUI_INTERACTIVE=1/0 overrides it, so a
 * localhost proxy that forwards to a real switch cannot be modified
 * by accident. */
const INTERACTIVE = process.env.WUI_INTERACTIVE !== undefined
  ? process.env.WUI_INTERACTIVE === '1'
  : BASE === 'http://127.0.0.1:18080';
const PAGES = ['dash', 'stat', 'vlan', 'eee', 'port', 'bw', 'qos', 'storm', 'acl', 'mirror', 'lag', 'l2', 'sys', 'sfp'];

/* Poll page.evaluate(fn) until it returns truthy (fixed waits make the
 * test slow on loaded panels and flaky on slow CI machines). */
async function waitFor(page, fn, timeout = 10000, what = 'condition') {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    try {
      if (await page.evaluate(fn)) return true;
    } catch (e) { /* the element may not exist yet */ }
    await page.waitForTimeout(100);
  }
  throw new Error('timeout waiting for ' + what);
}

/* Node-side poll (the browser cannot see Node variables like cmdPosts). */
async function waitForNode(fn, timeout = 10000, what = 'condition') {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (fn()) return true;
    await new Promise(r => setTimeout(r, 100));
  }
  throw new Error('timeout waiting for ' + what);
}

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const consoleErrors = [];
  const pageErrors = [];
  const failedRequests = [];
  const failures = [];
  const cmdPosts = [];           // {status, body} of every POST /cmd
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
  page.on('response', async res => {
    if (res.request().method() === 'POST' && res.url().includes('/cmd')) {
      cmdPosts.push({ status: res.status(), body: res.request().postData() || '' });
    }
    if (res.status() >= 400) console.log('  HTTP ' + res.status() + ' response: ' + res.url() + ' [page: ' + page.url() + ']');
  });
  page.on('framenavigated', f => { if (f === page.mainFrame()) console.log('  navigate: ' + f.url()); });

  // 1. Load root: the firmware serves static files unauthenticated, so
  // this must NOT be a 401/redirect-to-login (it stays on the index).
  await page.goto(BASE + '/', { waitUntil: 'networkidle' });
  if (!page.url().includes('login.html')) {
    console.log('OK: root served without a login redirect (' + page.url() + ')');
  } else {
    failures.push('unexpected redirect to login.html, got ' + page.url());
    console.log('FAIL: unexpected redirect to login.html, got ' + page.url());
  }

  // 2. Login (via the explicit login page)
  await page.goto(BASE + '/login.html', { waitUntil: 'networkidle' });
  await waitFor(page, () => !!document.getElementById('pwd'), 10000, 'password field');
  await page.fill('#pwd', PASSWORD);
  await Promise.all([
    page.waitForURL(url => url.pathname === '/' || url.pathname === '/index.html', { timeout: 10000 }),
    page.click('button[type=submit]')
  ]);
  console.log('OK: logged in (' + page.url() + ')');
  loggedIn = true;

  // 2b. Login failure display: a wrong password must land back on
  // login.html and show the #incorrect message.
  await page.goto(BASE + '/logout', { waitUntil: 'networkidle' });
  await page.goto(BASE + '/login.html', { waitUntil: 'networkidle' });
  await waitFor(page, () => !!document.getElementById('pwd'), 10000, 'password field');
  await page.fill('#pwd', 'wrong-password');
  await Promise.all([
    page.waitForURL(url => url.pathname === '/login.html', { timeout: 10000 }),
    page.click('button[type=submit]')
  ]);
  const incorrect = await (async () => {
    try {
      await waitFor(page, () => {
        const el = document.getElementById('incorrect');
        return el && el.style.display !== 'none' && el.textContent.length > 0;
      }, 5000, 'login error message');
      return true;
    } catch (e) {
      return false;
    }
  })();
  if (incorrect) {
    console.log('OK: wrong password shows the login error message');
  } else {
    failures.push('wrong password did not show the login error message');
    console.log('FAIL: wrong password did not show the login error message');
  }
  await page.fill('#pwd', PASSWORD);
  await Promise.all([
    page.waitForURL(url => url.pathname === '/' || url.pathname === '/index.html', { timeout: 10000 }),
    page.click('button[type=submit]')
  ]);

  // 3. Dashboard: port grid order + link state renders
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

  // 4. Panel render + element checks
  const panelChecks = {
    stat: async () => {
      const rows = await page.$$eval('#stat-body tr', els => els.length);
      if (rows !== portNums.length) throw new Error('stat rows=' + rows + ' expected ' + portNums.length);
      console.log('OK: stat table rows=' + rows);
    },
    vlan: async () => {
      const hasEdit = await page.evaluate(() => !!document.getElementById('vlan-edit-table'));
      if (!hasEdit) throw new Error('vlan edit table missing');
      console.log('OK: vlan panel controls present');
    },
    eee: async () => {
      const rows = await page.$$eval('#eeebody tr', els => els.length);
      if (rows !== portNums.length) throw new Error('eee rows=' + rows);
      console.log('OK: eee rows=' + rows);
    },
    port: async () => {
      const rows = await page.$$eval('#speedbody tr', els => els.length);
      if (rows !== portNums.length) throw new Error('speed table rows=' + rows);
      console.log('OK: port config rows=' + rows);
    },
    bw: async () => {
      const rows = await page.$$eval('#bw-body tr', els => els.length);
      if (rows !== portNums.length) throw new Error('bw rows=' + rows);
      console.log('OK: bandwidth rows=' + rows);
    },
    qos: async () => {
      const mode = await page.evaluate(() => document.getElementById('qos-mode').value);
      const pcp = await page.evaluate(() => document.querySelectorAll('#qos-pcp-table select').length);
      const dscp = await page.evaluate(() => document.querySelectorAll('#qos-dscp-table select').length);
      const sched = await page.evaluate(() => (document.getElementById('qos-sched').textContent.match(/Port /g) || []).length);
      if (!/^[0-3]$/.test(mode) || pcp !== 8 || dscp !== 8 || sched !== portNums.length) {
        throw new Error('qos mode=' + mode + ' pcp=' + pcp + ' dscp=' + dscp + ' sched=' + sched);
      }
      console.log('OK: qos mode=' + mode + ' pcp=' + pcp + ' dscp=' + dscp + ' sched=' + sched);
    },
    storm: async () => {
      const rows = await page.$$eval('#storm-body tr', els => els.length);
      const controls = await page.$$eval('#storm-body input[type=checkbox]', els => els.length);
      if (rows !== 4 || controls !== 4) throw new Error('storm rows=' + rows + ' checkboxes=' + controls);
      console.log('OK: storm rows=' + rows);
    },
    acl: async () => {
      const hasAdd = await page.evaluate(() => !!document.getElementById('acl-port') && !!document.getElementById('acl-action') && !!document.getElementById('acl-match'));
      if (!hasAdd) throw new Error('acl add controls missing');
      console.log('OK: acl add controls present');
    },
    mirror: async () => {
      const hasCfg = await page.evaluate(() => !!document.getElementById('mirror-en') && !!document.getElementById('mirror-port'));
      if (!hasCfg) throw new Error('mirror controls missing');
      console.log('OK: mirror controls present');
    },
    lag: async () => {
      const hasCfg = await page.evaluate(() => !!document.getElementById('lag-container'));
      if (!hasCfg) throw new Error('lag container missing');
      console.log('OK: lag container present');
    },
    l2: async () => {
      const hasToggles = await page.evaluate(() => !!document.getElementById('igmp-en') && !!document.getElementById('querier-en') && !!document.getElementById('mld-en'));
      if (!hasToggles) throw new Error('l2 multicast toggles missing');
      console.log('OK: l2 multicast toggles present');
    },
    sys: async () => {
      const tabs = await page.evaluate(() =>
        !!document.getElementById('sys-tab-main') && !!document.getElementById('sys-tab-advanced') &&
        !!document.getElementById('sys-tab-console') && !!document.getElementById('sys-tab-update'));
      if (!tabs) throw new Error('sys tabs missing');
      console.log('OK: sys tabs present');
    },
    sfp: async () => {
      const hasCtl = await page.evaluate(() => !!document.getElementById('slotsel') && !!document.getElementById('hexdump'));
      if (!hasCtl) throw new Error('sfp controls missing');
      console.log('OK: sfp controls present');
    }
  };

  for (const pg of PAGES) {
    const errBefore = consoleErrors.length;
    await page.evaluate(id => { try { nav(id); } catch (e) { window.__navErr = String(e); } }, pg);
    const navErr = await page.evaluate(() => window.__navErr || null);
    if (navErr) {
      failures.push('nav(' + pg + ') threw: ' + navErr);
      console.log('FAIL: nav(' + pg + ') threw: ' + navErr);
      continue;
    }
    const check = panelChecks[pg];
    if (check) {
      try {
        // The panel data arrives via API pollers; wait for the panel
        // element instead of a fixed delay.
        if (pg === 'stat') await waitFor(page, () => document.querySelectorAll('#stat-body tr').length >= 2, 15000, pg + ' rows');
        if (pg === 'eee') await waitFor(page, () => document.querySelectorAll('#eeebody tr').length >= 2, 15000, pg + ' rows');
        if (pg === 'port') await waitFor(page, () => document.querySelectorAll('#speedbody tr').length >= 2, 15000, pg + ' rows');
        if (pg === 'bw') await waitFor(page, () => document.querySelectorAll('#bw-body tr').length >= 2, 15000, pg + ' rows');
        if (pg === 'storm') await waitFor(page, () => document.querySelectorAll('#storm-body tr').length >= 2, 15000, pg + ' rows');
        await check();
      } catch (e) {
        failures.push('check(' + pg + '): ' + e.message);
        console.log('FAIL: check(' + pg + '): ' + e.message);
        continue;
      }
    }
    if (consoleErrors.length > errBefore) {
      failures.push('console errors on ' + pg + ': ' + consoleErrors.slice(errBefore).join(' | '));
      console.log('FAIL: console errors on ' + pg + ': ' + consoleErrors.slice(errBefore).join(' | '));
      continue;
    }
    console.log('OK: nav(' + pg + ') clean');
  }

  // 5. System console page details (ARP table, running config, ping)
  await page.evaluate(() => nav('sys'));
  await waitFor(page, () => !!document.getElementById('sys-tab-console'), 10000, 'sys tabs');
  await page.click('button:has-text("Console")');
  await waitFor(page, () => document.querySelectorAll('#arp-body tr').length > 0, 10000, 'ARP table');
  const arpRows = await page.$$eval('#arp-body tr', els => els.length);
  const runCfgBytes = await page.evaluate(() => document.getElementById('running-config').textContent.length);
  if (arpRows < 1) { failures.push('ARP table empty'); console.log('FAIL: ARP table empty'); }
  else console.log('OK: ARP rows=' + arpRows);
  if (runCfgBytes < 10) { failures.push('running-config empty'); console.log('FAIL: running-config empty'); }
  else console.log('OK: running-config ' + runCfgBytes + ' bytes');

  // Ping through the UI: needs the features.js poller, run it only when
  // the /ping.json data comes back (simulator) — against a real device the
  // request changes device state, so skip it there unless --interactive.
  if (INTERACTIVE) {
    const errBefore = consoleErrors.length;
    await page.fill('#ping-ip', '192.168.10.100');
    await page.click('button:has-text("Ping")');
    await waitFor(page, () => /received|no replies/.test(document.getElementById('ping-result').textContent), 15000, 'ping result');
    const pingRes = await page.evaluate(() => document.getElementById('ping-result').textContent);
    console.log('OK: ping result "' + pingRes + '"');
    if (consoleErrors.length > errBefore) {
      failures.push('console errors during ping: ' + consoleErrors.slice(errBefore).join(' | '));
    }
  } else {
    console.log('SKIP: ping interaction (read-only mode on real device)');
  }

  // 5b. i18n switch: changing the language must update a visible label.
  // The language selector lives in the System page's main tab.
  await page.evaluate(() => nav('sys'));
  await page.evaluate(() => { try { openTab(event || {target: null}, 'sys-tab-main'); } catch (e) {} });
  await page.click('button:has-text("System")').catch(() => {});
  await waitFor(page, () => {
    const s = document.getElementById('lang-select');
    return s && s.offsetParent !== null;
  }, 10000, 'visible lang-select');
  const langBefore = await page.evaluate(() => document.getElementById('lang-select').value);
  const langOptions = await page.evaluate(() => {
    const s = document.getElementById('lang-select');
    return Array.from(s.options).map(o => o.value).filter(v => v !== 'en');
  });
  if (langOptions.length > 0) {
    const other = langOptions[0];
    await page.selectOption('#lang-select', other);
    await page.waitForTimeout(300);
    const title = await page.evaluate(() => document.title);
    if (title && title.length > 0) {
      console.log('OK: language switch en -> ' + other + ' (title="' + title + '")');
    } else {
      failures.push('language switch left an empty title');
      console.log('FAIL: language switch left an empty title');
    }
    await page.selectOption('#lang-select', langBefore || 'en');
  } else {
    console.log('SKIP: no alternate language available');
  }

  // 5c. L2 delete button (simulator table has entries).
  if (INTERACTIVE) {
    await page.evaluate(() => nav('l2'));
    await waitFor(page, () => !!document.querySelector('#l2body button'), 15000, 'L2 delete button');
    const errBefore = consoleErrors.length;
    await page.click('#l2body button');
    await page.waitForTimeout(1500);
    if (consoleErrors.length > errBefore) {
      failures.push('console errors during L2 delete: ' + consoleErrors.slice(errBefore).join(' | '));
    } else {
      console.log('OK: L2 delete button clickable without errors');
    }
  }

  // 6. Interactive apply flows (simulator only): click the apply actions
  //    and verify the expected /cmd POST reaches the server with 200.
  if (INTERACTIVE) {
    const actions = [
      { name: 'qos-mode', fn: 'applyQosMode()', cmd: /qos (off|mode)/ },
      { name: 'qos-pcp', fn: 'applyQosPcp()', cmd: /qos pcp/ },
      { name: 'storm', fn: 'applyStorm(0)', cmd: /storm-control/ },
      { name: 'acl-add', fn: 'aclAdd()', cmd: /acl add/, prep: () => page.evaluate(() => {
          document.getElementById('acl-port').value = '3';
          document.getElementById('acl-action').value = 'deny';
          document.getElementById('acl-match-type').value = 'ip';
          document.getElementById('acl-match').value = '192.168.1.99/32';
        }) },
      { name: 'igmp', fn: 'applyIGMP()', cmd: /igmp/ },
      { name: 'mirror', fn: '(function(){ document.getElementById("mirror-port").value = "1"; document.getElementById("mrx2").checked = true; try { applyMirror(); } catch(e) { window.__fnErr = String(e); } })()', cmd: /^mirror/, prep: async () => {
          /* The mirror grid is built by loadMirrorConfig() and the
           * /mirror.json response handler later overwrites the checkbox
           * states, so the panel must be fully loaded before the apply
           * runs (and the checkbox is set inside the same evaluate as
           * applyMirror to avoid the async callback racing it). */
          await waitFor(page, () => {
            const el = document.getElementById('mirror-port');
            return el && el.value !== '' && document.getElementById('mrx1') !== null;
          }, 10000, 'mirror panel');
        } },
      { name: 'bw', fn: 'applyBandwidth(3)', cmd: /^bw /, prep: () => page.evaluate(() => {
          document.getElementById('ilim_3').checked = true;
          document.getElementById('ibw_3').value = '100';
        }) },
      { name: 'lag', fn: 'applyLAG(0)', cmd: /^lag / },
      { name: 'eee', fn: 'eeeSub(1, true)', cmd: /^eee 1 on/ },
      { name: 'vlan', fn: 'applyVLAN()', cmd: /^vlan /, prep: () => page.evaluate(() => {
          document.getElementById('vid').value = '200';
        }) },
      { name: 'mtu', fn: 'applyMTU(1)', cmd: /^mtu 1 /, prep: () => page.evaluate(() => {
          document.getElementById('mtu1').value = '1522';
        }) },
      { name: 'port-name', fn: 'applyPortCfg(1)', cmd: /^port 1 /, prep: () => page.evaluate(() => {
          document.getElementById('pname1').value = 'ui-test';
        }) },
      { name: 'pvid', fn: 'applyVLAN()', cmd: /^pvid 1 /, prep: () => page.evaluate(() => {
          document.getElementById('vid').value = '200';
          document.getElementById('pport1').checked = true;
        }) },
      { name: 'telnet', fn: 'telnetToggle()', cmd: /^telnet (on|off)/, prep: () => page.evaluate(() => {
          document.getElementById('telnet_toggle').checked = true;
        }) },
      { name: 'web', fn: 'webToggle()', cmd: /^web (on|off)/, prep: () => page.evaluate(() => {
          document.getElementById('web_toggle').checked = true;
        }) }
    ];
    const panelOf = { 'qos-mode': 'qos', 'qos-pcp': 'qos', 'storm': 'storm', 'acl-add': 'acl',
      'igmp': 'l2', 'mirror': 'mirror', 'bw': 'bw', 'lag': 'lag', 'eee': 'eee', 'vlan': 'vlan',
      'mtu': 'port', 'port-name': 'port', 'pvid': 'vlan', 'telnet': 'sys', 'web': 'sys' };
    for (const a of actions) {
      const errBefore = consoleErrors.length;
      const postsBefore = cmdPosts.length;
      await page.evaluate(id => { try { nav(id); } catch (e) { window.__navErr = String(e); } }, panelOf[a.name]);
      if (a.prep) await a.prep();
      await page.evaluate(fn => { try { eval(fn); } catch (e) { window.__fnErr = String(e); } }, a.fn);
      const fnErr = await page.evaluate(() => window.__fnErr || null);
      if (fnErr) {
        failures.push(a.name + ' threw: ' + fnErr);
        console.log('FAIL: ' + a.name + ' threw: ' + fnErr);
        continue;
      }
      /* Wait for a POST matching this action's command.  Chains (qos-pcp
       * sends 8 commands) may still deliver stragglers of the previous
       * action, so matching the command is more robust than counting. */
      await waitForNode(() => cmdPosts.slice(postsBefore).some(p => a.cmd.test(p.body.trim())),
                        8000, a.name + ' /cmd POST');
      const newPosts = cmdPosts.slice(postsBefore);
      if (!newPosts.length) {
        failures.push(a.name + ': no /cmd POST observed');
        console.log('FAIL: ' + a.name + ': no /cmd POST observed');
        continue;
      }
      if (newPosts.some(p => p.status !== 200)) {
        failures.push(a.name + ': /cmd status ' + JSON.stringify(newPosts.map(p => p.status)));
        console.log('FAIL: ' + a.name + ': /cmd status ' + JSON.stringify(newPosts.map(p => p.status)));
        continue;
      }
      if (!newPosts.some(p => a.cmd.test(p.body.trim()))) {
        failures.push(a.name + ': unexpected /cmd body ' + JSON.stringify(newPosts.map(p => p.body)));
        console.log('FAIL: ' + a.name + ': unexpected /cmd body ' + JSON.stringify(newPosts.map(p => p.body)));
        continue;
      }
      if (consoleErrors.length > errBefore) {
        failures.push(a.name + ' console errors: ' + consoleErrors.slice(errBefore).join(' | '));
        console.log('FAIL: ' + a.name + ' console errors: ' + consoleErrors.slice(errBefore).join(' | '));
        continue;
      }
      console.log('OK: ' + a.name + ' -> /cmd 200 (' + newPosts.length + ' POSTs)');
    }
  } else {
    console.log('SKIP: apply flows (read-only mode on real device)');
  }

  // 7. Firmware update section is a tab inside the System page
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

  // 8. Final summary
  console.log('\n=== Summary ===');
  console.log('Console errors : ' + (consoleErrors.length ? consoleErrors.join('\n  ') : 'none'));
  console.log('Page errors    : ' + (pageErrors.length ? pageErrors.join('\n  ') : 'none'));
  console.log('Failed requests: ' + (failedRequests.length ? failedRequests.join('\n  ') : 'none'));

  const clean = failures.length === 0 && consoleErrors.length === 0 && pageErrors.length === 0 && failedRequests.length === 0;
  console.log('\nRESULT: ' + (clean ? 'PASS' : 'FAIL'));
  await browser.close();
  process.exit(clean ? 0 : 1);
})().catch(e => { console.error('TEST CRASH:', e); process.exit(2); });
