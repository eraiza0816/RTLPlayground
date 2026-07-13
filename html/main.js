/** NOTIFICATIONS **/
let toastTimer;
function notify(msg, type) {
  type = type || 'info';
  var t = document.getElementById('toast');
  clearTimeout(toastTimer);
  t.className = 'show ' + type;
  t.innerText = msg;
  toastTimer = setTimeout(function() { t.className = t.className.replace('show', '').trim(); }, 4000);
}

/** FETCH API QUEUE (serialized, deduplicated) **/
var reqQ = [], busy = false, inFlight = null;
var poller = null, systemInterval = null, l2Interval = null;
var isFlashing = false;

function fetchAPI(method, url, cb, data) {
  if (isFlashing) return;
  var key = url + '|' + method + '|' + (data || '');
  if (inFlight && inFlight.key === key) return;
  if (reqQ.some(function(r) { return (r.url + '|' + r.method + '|' + (r.data || '')) === key; })) return;
  reqQ.push({ method: method, url: url, cb: cb, data: data, key: key });
  if (!busy) processQ();
}

function processQ() {
  if (reqQ.length === 0 || isFlashing) { busy = false; return; }
  busy = true;
  var r = reqQ.shift();
  inFlight = { url: r.url, method: r.method, key: r.key };
  var finalUrl = r.url;
  if (r.method === 'GET') finalUrl += (finalUrl.indexOf('?') >= 0 ? '&' : '?') + '_t=' + new Date().getTime();
  var x = new XMLHttpRequest();
  x.open(r.method, finalUrl, true);
  x.timeout = 4000;
  x.onreadystatechange = function() {
    if (x.readyState === 4) {
      inFlight = null;
      if (isFlashing) return;
      if (x.status === 200 && r.cb) r.cb(x.responseText);
      else if (x.status === 401) { document.location = '/login.html'; return; }
      setTimeout(processQ, 50);
    }
  };
  x.ontimeout = x.onerror = function() { inFlight = null; setTimeout(processQ, 100); };
  x.send(r.data);
}

/** GLOBALS **/
var globalNumPorts = 0;
var physToLogMap = {};
var logToPhysMap = {};
var sfpMap = {};
var portNames = {};
var pState = [];
var pAdvertised = [];
var txG = [], txB = [], rxG = [], rxB = [];
var linkS = ["Disabled", "Down", "10M", "100M", "1000M", "500M", "10G", "2.5G", "5G"];
var l2Entries = [];
var l2CurrentEntry = 0;
var configuration = [];
var mtus = [];

const sysLabels = {
  ip_address: "IP Address", ip_netmask: "Subnet Mask", ip_gateway: "Default Gateway",
  sw_ver: "Firmware Version", hw_ver: "Hardware Version", mac_addr: "MAC Address", uptime: "System Uptime"
};

/** NAVIGATION & SMART POLLING **/
function nav(id) {
  if (isFlushing) return notify("Locked during update.", "warning");
  clearInterval(poller); poller = null;
  clearInterval(systemInterval); systemInterval = null;
  clearInterval(l2Interval); l2Interval = null;

  document.querySelectorAll('.panel').forEach(function(e) { e.classList.remove('active'); });
  document.querySelectorAll('#nav li').forEach(function(e) { e.classList.remove('active'); });

  document.getElementById(id).classList.add('active');
  var navEl = document.getElementById('nav-' + id);
  if (navEl) navEl.classList.add('active');

  if (id === 'dash') {
    pollStatus(); poller = setInterval(pollStatus, 5000);
    pollInfo(); systemInterval = setInterval(pollInfo, 5000);
  } else if (id === 'port') {
    pollStatus(); poller = setInterval(pollStatus, 5000);
    loadPortConfig();
  } else if (id === 'stat') {
    pollStatus(); poller = setInterval(pollStatus, 5000);
  } else if (id === 'vlan') {
    initVlanTable();
    loadVlanList();
  } else if (id === 'l2') {
    loadL2Config();
  } else if (id === 'mirror') {
    loadMirrorConfig();
  } else if (id === 'lag') {
    loadLagConfig();
  } else if (id === 'eee') {
    pollStatus(); poller = setInterval(pollStatus, 5000);
    loadEeeConfig();
  } else if (id === 'bw') {
    loadBwConfig();
  } else if (id === 'sfp') {
    loadEeprom();
  } else if (id === 'sys') {
    pollInfo(); systemInterval = setInterval(pollInfo, 5000);
    loadSysConfig();
  }
}

/** LIVE DATA POLLERS **/
function pollStatus() {
  fetchAPI('GET', '/status.json', function(raw) {
    try {
      var data = JSON.parse(raw);
      var grid = document.getElementById('port-grid');
      var sBody = document.getElementById('stat-body');
      var isDash = document.getElementById('dash').classList.contains('active');
      var isStat = document.getElementById('stat').classList.contains('active');

      if (!globalNumPorts || globalNumPorts !== data.length) {
        globalNumPorts = data.length;
        txG = new Array(data.length);
        txB = new Array(data.length);
        rxG = new Array(data.length);
        rxB = new Array(data.length);
        pState = new Array(data.length);
        pAdvertised = new Array(data.length);
        if (isDash) grid.innerHTML = '';
        if (isStat) sBody.innerHTML = '';
      }

      if (isDash && !grid.children.length && data.length > 0) {
        data.forEach(function(p) {
          if (isNaN(parseInt(p.portNum, 10)) && isNaN(parseInt(p.logPort, 10))) return;
          var d = document.createElement('div');
          d.id = 'port-' + p.portNum;
          d.className = 'port';
          grid.appendChild(d);
        });
      }

      if (isStat && (!sBody.children.length || data.length !== sBody.children.length)) {
        sBody.innerHTML = '';
        data.forEach(function(p) {
          if (isNaN(parseInt(p.portNum, 10))) return;
          var tr = document.createElement('tr');
          tr.id = 'stat-' + p.portNum;
          for (var i = 0; i < 8; i++) tr.appendChild(document.createElement('td'));
          sBody.appendChild(tr);
        });
      }

      data.forEach(function(p) {
        var portNum = parseInt(p.portNum, 10);
        if (isNaN(portNum)) return;
        var logPort = parseInt(p.logPort, 10);
        physToLogMap[portNum] = logPort;
        logToPhysMap[logPort] = portNum;
        sfpMap[portNum] = p.isSFP;
        portNames[portNum] = p.name || '';

        var linkRaw = parseInt(p.link, 10);
        var linkText = 'UNKNOWN';
        var isUp = false;
        var idx = portNum - 1;

        if (p.enabled == 0) { linkText = 'Disabled'; pState[idx] = -1; }
        else if (!isNaN(linkRaw)) { isUp = linkRaw > 0; linkText = linkS[linkRaw + 1] || 'UNKNOWN'; pState[idx] = linkRaw; }

        txG[idx] = p.txG ? BigInt(p.txG) : 0n;
        txB[idx] = p.txB ? BigInt(p.txB) : 0n;
        rxG[idx] = p.rxG ? BigInt(p.rxG) : 0n;
        rxB[idx] = p.rxB ? BigInt(p.rxB) : 0n;

        if (isDash) {
          var d = document.getElementById('port-' + portNum);
          if (d) {
            d.className = isUp ? 'port up' : 'port';
            var tooltip = 'Port ' + portNum;
            if (portNames[portNum]) tooltip += ' (' + portNames[portNum] + ')';
            tooltip += '\nLink: ' + linkText;
            if (isUp) {
              tooltip += '\nTx: ' + (p.txG ? BigInt(p.txG).toString() : '0') + ' pkts';
              tooltip += '\nRx: ' + (p.rxG ? BigInt(p.rxG).toString() : '0') + ' pkts';
            }
            if (p.isSFP) {
              tooltip += '\n\n-- SFP Diagnostics --';
              if (p.sfp_vendor) tooltip += '\nVendor: ' + p.sfp_vendor + ' (' + p.sfp_model + ')';
              if (p.sfp_temp && p.sfp_temp !== '0x0000') {
                var tRaw = parseInt(p.sfp_temp, 16);
                if (tRaw > 32767) tRaw -= 65536;
                tooltip += '\nTemp: ' + (tRaw / 256).toFixed(1) + ' °C';
              }
              if (p.sfp_vcc && p.sfp_vcc !== '0x0000') tooltip += '\nVcc: ' + (parseInt(p.sfp_vcc, 16) * 0.0001).toFixed(2) + ' V';
              if (p.sfp_txbias && p.sfp_txbias !== '0x0000') tooltip += '\nTx Bias: ' + (parseInt(p.sfp_txbias, 16) * 0.002).toFixed(2) + ' mA';
              if (p.sfp_txpower && p.sfp_txpower !== '0x0000') {
                var mw = parseInt(p.sfp_txpower, 16) * 0.0001;
                tooltip += '\nTx Power: ' + (mw > 0 ? (10 * Math.log10(mw)).toFixed(2) : '-inf') + ' dBm';
              }
              if (p.sfp_rxpower && p.sfp_rxpower !== '0x0000') {
                var mw2 = parseInt(p.sfp_rxpower, 16) * 0.0001;
                tooltip += '\nRx Power: ' + (mw2 > 0 ? (10 * Math.log10(mw2)).toFixed(2) : '-inf') + ' dBm';
              } else if (p.sfp_rxpower === '0x0000') {
                tooltip += '\nRx Power: LOS (No light)';
              }
              if (p.sfp_los !== null) tooltip += '\nRX-LOS: ' + Boolean(Number(p.sfp_los));
            }
            d.title = tooltip;
            d.innerHTML = '';
            if (p.isSFP) {
              var badge = document.createElement('div');
              badge.className = 'sfp-badge';
              badge.textContent = 'SFP';
              d.appendChild(badge);
              d.appendChild(document.createElement('br'));
            }
            d.appendChild(document.createTextNode('PORT ' + portNum));
            if (portNames[portNum]) {
              var ns = document.createElement('div');
              ns.style.cssText = 'font-size:11px;color:' + (isUp ? '#065f46' : '#94a3b8') + ';margin-top:2px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;';
              ns.textContent = portNames[portNum];
              d.appendChild(ns);
            }
            d.appendChild(document.createElement('br'));
            var span = document.createElement('span');
            span.style.fontSize = '16px';
            span.textContent = linkText;
            d.appendChild(span);
          }
        }

        if (isStat) {
          var tr = document.getElementById('stat-' + portNum);
          if (tr) {
            tr.cells[0].innerHTML = '<strong>' + (t('common_port') || 'Port ') + portNum + '</strong>';
            tr.cells[1].textContent = portNames[portNum];
            tr.cells[2].textContent = linkText;
            tr.cells[3].textContent = txG[idx].toString();
            tr.cells[3].style.color = 'var(--success)';
            tr.cells[4].textContent = txB[idx].toString();
            tr.cells[4].style.color = 'var(--danger)';
            tr.cells[5].textContent = rxG[idx].toString();
            tr.cells[5].style.color = 'var(--success)';
            tr.cells[6].textContent = rxB[idx].toString();
            tr.cells[6].style.color = 'var(--danger)';
            tr.cells[7].innerHTML = '<button class="btn" onclick="showCounters(' + portNum + ')" style="padding:4px 8px;font-size:11px;">' + (t('stat_show') || 'Show') + '</button>';
          }
        }
      });
    } catch (e) { console.warn(e); }
  });
}

function pollInfo() {
  fetchAPI('GET', '/information.json', function(raw) {
    try {
      var data = JSON.parse(raw);
      var tbl = document.getElementById('info-table');
      if (tbl) {
        tbl.innerHTML = '';
        for (var k in data) {
          if (!data.hasOwnProperty(k)) continue;
          var row = tbl.insertRow();
          var kc = row.insertCell();
          var strong = document.createElement('strong');
          strong.textContent = sysLabels[k] || k;
          kc.appendChild(strong);
          row.insertCell().textContent = data[k];
        }
      }
      ['sys-ip','sys-mask','sys-gw'].forEach(function(id) {
        var el = document.getElementById(id);
        if (el && document.activeElement !== el) {
          var keyMap = { 'sys-ip': 'ip_address', 'sys-mask': 'ip_netmask', 'sys-gw': 'ip_gateway' };
          el.value = data[keyMap[id]] || '';
        }
      });
    } catch (e) {}
  });
}

/** PORT CONFIG **/
function loadPortConfig() {
  fetchAPI('GET', '/mtu.json', function(mtuRaw) {
    try {
      var tbody = document.getElementById('speedbody');
      tbody.innerHTML = '';
      var mtuData = JSON.parse(mtuRaw);
      var mtuRow = document.getElementById('mtutable');
      if (!mtuRow) return;

      var mtus = {};
      var headerRow = document.createElement('tr');
      var applyRow = document.createElement('tr');

      mtuData.forEach(function(p) {
        var portNum = parseInt(p.portNum, 10);
        if (isNaN(portNum)) return;
        mtus[portNum] = parseInt(p.mtu, 16);

        var tr = document.createElement('tr');
        tr.id = 'pcfg-row-' + portNum;
        tr.innerHTML = '<td><strong>' + (t('common_port') || 'Port ') + portNum + '</strong></td>'
          + '<td><input type="text" id="pname' + portNum + '" style="width:90px;" placeholder="' + (t('port_name') || 'Name') + '" value="' + (portNames[portNum] || '') + '"></td>'
          + '<td id="link-' + portNum + '">' + linkS[(pState[portNum - 1] || 0) + 1] + '</td>'
          + '<td><select id="speed' + portNum + '" style="width:120px;">'
          + '<option value="auto">' + (t('port_auto') || 'Auto') + '</option>'
          + '<option value="off">' + (t('speed_disabled') || 'Disabled') + '</option>'
          + '<option value="2g5">' + (t('port_2500m') || '2500MBit/Full') + '</option>'
          + '<option value="1g">' + (t('port_1000m') || '1000MBit/Full') + '</option>'
          + '<option value="100m full">' + (t('port_100m_f') || '100MBit/Full') + '</option>'
          + '<option value="100m half">' + (t('port_100m_h') || '100MBit/Half') + '</option>'
          + '<option value="10m full">' + (t('port_10m_f') || '10MBit/Full') + '</option>'
          + '<option value="10m half">' + (t('port_10m_h') || '10MBit/Half') + '</option>'
          + '</select></td>'
          + '<td class="cb-cell"><input type="checkbox" id="disable' + portNum + '" onchange="togglePort(' + portNum + ')"></td>'
          + '<td><button class="btn" id="pcfg-btn-' + portNum + '" style="padding:4px 8px;font-size:11px;" onclick="applyPortCfg(' + portNum + ')">' + (t('port_apply') || 'Apply') + '</button></td>';
        tbody.appendChild(tr);

        var th = document.createElement('th');
        th.style.textAlign = 'center';
        th.innerHTML = '<span style="font-size:10px;">Port ' + portNum + '</span>';
        headerRow.appendChild(th);

        var td = document.createElement('td');
        td.style.textAlign = 'center';
        td.innerHTML = '<select id="mtu' + portNum + '" style="width:80px;" onchange="applyMTU(' + portNum + ')">'
          + '<option value="16383"' + (mtus[portNum] === 16383 ? ' selected' : '') + '>16383</option>'
          + '<option value="1500"' + (mtus[portNum] === 1500 ? ' selected' : '') + '>1500</option>'
          + '<option value="1522"' + (mtus[portNum] === 1522 ? ' selected' : '') + '>1522</option>'
          + '<option value="1536"' + (mtus[portNum] === 1536 ? ' selected' : '') + '>1536</option>'
          + '<option value="1552"' + (mtus[portNum] === 1552 ? ' selected' : '') + '>1552</option>'
          + '<option value="9216"' + (mtus[portNum] === 9216 ? ' selected' : '') + '>9216</option>'
          + '</select>';
        applyRow.appendChild(td);
      });
      mtuRow.innerHTML = '';
      mtuRow.appendChild(headerRow);
      mtuRow.appendChild(applyRow);

      // Also load EEE config for global EEE
      fetchAPI('GET', '/config', function(raw) {
        var clean = raw.replace(/[^\x09\x0A\x0D\x20-\x7E]/g, '').trim();
        parseConf(clean);
      });
    } catch (e) { console.warn(e); }
  });
}

function togglePort(p) {
  var disabled = document.getElementById('disable' + p).checked;
  document.getElementById('speed' + p).disabled = disabled;
}

async function applyPortCfg(p) {
  var btn = document.getElementById('pcfg-btn-' + p);
  if (btn) btn.disabled = true;
  var speed = document.getElementById('speed' + p).value;
  var disabled = document.getElementById('disable' + p).checked;
  var cmd = 'port ' + p + ' ' + (disabled ? 'off' : speed);
  var pname = document.getElementById('pname' + p).value.trim().replace(/\s+/g, '_');
  if (pname) fetchAPI('POST', '/cmd', null, 'port ' + p + ' name ' + pname);
  fetchAPI('POST', '/cmd', function() {
    if (btn) btn.disabled = false;
    notify('Port ' + p + ' ' + (t('port_col_apply') || 'applied') + '.', 'success');
  }, cmd);
}

function applyMTU(p) {
  var mtu = document.getElementById('mtu' + p).value;
  fetchAPI('POST', '/cmd', function() { notify('MTU for Port ' + p + ' updated.', 'success'); }, 'mtu ' + p + ' ' + mtu);
}

/** VLAN **/
function initVlanTable() {
  var tbody = document.getElementById('vlan-edit-body');
  if (!globalNumPorts) return;
  if (tbody.children.length === globalNumPorts && tbody.children[0].cells.length > 1) return;
  var rows = '';
  for (var i = 1; i <= globalNumPorts; i++) {
    rows += '<tr><td><strong>' + (t('common_port') || 'Port ') + i + '</strong></td>'
      + '<td class="cb-cell"><input type="checkbox" id="tport' + i + '" onclick="document.getElementById(\'uport' + i + '\').checked=false"></td>'
      + '<td class="cb-cell"><input type="checkbox" id="uport' + i + '" onclick="document.getElementById(\'tport' + i + '\').checked=false"></td>'
      + '<td class="cb-cell"><input type="checkbox" id="pport' + i + '"></td></tr>';
  }
  tbody.innerHTML = rows;
}

function fetchVLAN() {
  var vid = document.getElementById('vid').value;
  if (!vid) return notify('Please enter a VLAN ID first.', 'warning');
  fetchAPI('GET', '/vlan.json?vid=' + vid, function(raw) {
    try {
      var s = JSON.parse(raw);
      document.getElementById('vname').value = s.name || '';
      var members = parseInt(s.members, 16);
      var untag = s.untag !== undefined ? parseInt(s.untag, 16) : ((members >> 10) & 0x3FF);
      var pvidMask = parseInt(s.pvid, 16);
      for (var p = 1; p <= globalNumPorts; p++) {
        var bit = physToLogMap[p] !== undefined ? physToLogMap[p] : (p - 1);
        document.getElementById('tport' + p).checked = ((members >> bit) & 1) && !((untag >> bit) & 1);
        document.getElementById('uport' + p).checked = ((members >> bit) & 1) && ((untag >> bit) & 1);
        document.getElementById('pport' + p).checked = (pvidMask >> bit) & 1;
      }
      notify('VLAN ' + vid + ' loaded.', 'success');
    } catch (e) { notify('VLAN not found.', 'error'); }
  });
}

function applyVLAN() {
  var vid = document.getElementById('vid').value;
  if (!vid) return notify('Please enter a VLAN ID first.', 'warning');
  var vlanName = document.getElementById('vname').value.trim().replace(/\s+/g, '_');
  var cmd = 'vlan ' + vid;
  if (vlanName) cmd += ' ' + vlanName;
  for (var p = 1; p <= globalNumPorts; p++) {
    if (document.getElementById('tport' + p).checked) cmd += ' ' + p + 't';
    else if (document.getElementById('uport' + p).checked) cmd += ' ' + p;
  }
  fetchAPI('POST', '/cmd', function() {
    // Send PVID commands sequentially after VLAN command completes
    sendPvids(vid);
  }, cmd);
}

function sendPvids(vid) {
  for (var p = 1; p <= globalNumPorts; p++) {
    if (document.getElementById('pport' + p).checked) {
      fetchAPI('POST', '/cmd', null, 'pvid ' + p + ' ' + vid);
    }
  }
  notify('VLAN ' + vid + ' saved.', 'success');
  refreshVlanViews();
}

function loadVlanList() {
  fetchAPI('GET', '/vlanlist', function(raw) {
    try {
      var vlans = JSON.parse(raw);
      var sel = document.getElementById('vlanSelect');
      if (!vlans || !vlans.length) { sel.style.display = 'none'; return; }
      sel.style.display = '';
      sel.options.length = 1;
      vlans.forEach(function(v) {
        var opt = document.createElement('option');
        opt.value = v.id;
        opt.text = v.name ? v.id + ' — ' + v.name : String(v.id);
        sel.appendChild(opt);
      });
      sel.onchange = function() { document.getElementById('vid').value = this.value; fetchVLAN(); };
    } catch (e) { document.getElementById('vlanSelect').style.display = 'none'; }
  });
}

function refreshVlanViews() { loadVlanList(); loadVlanTable(); }

function portsToRange(mask, nPorts) {
  var parts = [], start = -1, prev = -1;
  for (var p = 1; p <= nPorts; p++) {
    var bit = physToLogMap[p] !== undefined ? physToLogMap[p] : (p - 1);
    if ((mask >> bit) & 1) { if (start < 0) start = p; prev = p; }
    else { if (start >= 0) { parts.push(start === prev ? String(start) : start + '-' + prev); start = -1; prev = -1; } }
  }
  if (start >= 0) parts.push(start === prev ? String(start) : start + '-' + prev);
  return parts.length ? parts.join(',') : '-';
}

function loadVlanTable() {
  var tbody = document.getElementById('vlanTableBody');
  if (!tbody) return;
  tbody.innerHTML = '';
  fetchAPI('GET', '/vlanlist', function(raw) {
    try {
      var vlans = JSON.parse(raw);
      vlans.forEach(function(v) {
        fetchAPI('GET', '/vlan.json?vid=' + v.id, function(sRaw) {
          try {
            var s = JSON.parse(sRaw);
            var m = parseInt(s.members, 16);
            var members = m & 0x3FF;
            var untag = s.untag !== undefined ? parseInt(s.untag, 16) : (((m >> 10) & 0x3FF) & members);
            var tagged = members & ~untag;
            var pvid = parseInt(s.pvid, 16) & 0x3FF;
            var tr = document.createElement('tr');
            var td = document.createElement('td');
            var a = document.createElement('a');
            a.href = '#';
            a.textContent = v.id;
            (function(vid) { a.onclick = function(e) { e.preventDefault(); document.getElementById('vid').value = vid; fetchVLAN(); }; })(v.id);
            td.appendChild(a); tr.appendChild(td);
            td = document.createElement('td'); td.textContent = v.name || ''; tr.appendChild(td);
            td = document.createElement('td'); td.textContent = portsToRange(members, globalNumPorts); tr.appendChild(td);
            td = document.createElement('td'); td.textContent = portsToRange(tagged, globalNumPorts); tr.appendChild(td);
            td = document.createElement('td'); td.textContent = portsToRange(untag, globalNumPorts); tr.appendChild(td);
            td = document.createElement('td'); td.textContent = portsToRange(pvid, globalNumPorts); tr.appendChild(td);
            td = document.createElement('td');
            if (v.id != 1) {
              var btn = document.createElement('button');
              btn.className = 'btn btn-danger';
              btn.style.cssText = 'padding:2px 8px;font-size:11px;';
              btn.textContent = '✕';
              (function(vid) { btn.onclick = function() { deleteVlan(vid); }; })(v.id);
              td.appendChild(btn);
            }
            tr.appendChild(td);
            tbody.appendChild(tr);
          } catch (e) {}
        });
      });
    } catch (e) {}
  });
}

function deleteVlan(id) {
  if (!confirm('Delete VLAN ' + id + '?')) return;
  fetchAPI('POST', '/cmd', function() { refreshVlanViews(); notify('VLAN ' + id + ' deleted.', 'success'); }, 'vlan ' + id + ' d');
}

/** L2 **/
function loadL2Config() {
  fetchAPI('GET', '/config', function(raw) {
    var clean = raw.replace(/[^\x09\x0A\x0D\x20-\x7E]/g, '').trim();
    parseConf(clean);
    var igmpMatch = clean.match(/^igmp\s+(on|off)/m);
    document.getElementById('igmp-en').checked = igmpMatch && igmpMatch[1] === 'on';
  });
  l2Entries = [];
  l2CurrentEntry = 0;
  getL2();
  if (l2Interval) clearInterval(l2Interval);
  l2Interval = setInterval(getL2, 2000);
}

function getL2() {
  fetchAPI('GET', '/l2.json?idx=' + l2CurrentEntry, function(raw) {
    try {
      var s = JSON.parse(raw).map(function(e) {
        e.vlan = parseInt(e.vlan, 16);
        e.idx = parseInt(e.idx, 16);
        e.type = e.type === 's' ? 'static' : 'learned';
        e.port = e.port == 9 ? 9 : (logToPhysMap[e.port] !== undefined ? logToPhysMap[e.port] : e.port);
        return e;
      });
      if (!s.length) return;
      l2Entries.push.apply(l2Entries, s);
      if (l2Entries.length >= 4096) { l2Entries = []; l2CurrentEntry = 0; clearInterval(l2Interval); return; }
      var w = 0;
      for (var i = l2Entries.length - 1; i > 0; i--) { if (l2Entries[0].idx == l2Entries[i].idx) { w = 1; break; } }
      if (w) { l2CurrentEntry = 0; fillL2(l2Entries); }
      else { l2CurrentEntry = s[s.length - 1].idx + 1; }
    } catch (e) {}
  });
}

function fillL2(s) {
  var tbody = document.getElementById('l2body');
  if (!tbody) return;
  s.sort(function(a, b) {
    if (a.port < b.port) return -1;
    if (a.port > b.port) return 1;
    if (a.mac < b.mac) return -1;
    if (a.mac > b.mac) return 1;
    if (a.vlan < b.vlan) return -1;
    if (a.vlan > b.vlan) return 1;
    return 0;
  });
  s = s.filter(function(item, pos, ary) { return !pos || item.idx != ary[pos - 1].idx; });
  s = s.map(function(e) { e.port = e.port != 9 ? e.port : 'CPU'; return e; });
  tbody.innerHTML = '';
  s.forEach(function(e) {
    var tr = document.createElement('tr');
    var td = tr.insertCell(); td.textContent = e.port;
    td = tr.insertCell(); td.textContent = e.mac;
    td = tr.insertCell(); td.textContent = e.vlan;
    td = tr.insertCell(); td.textContent = e.type;
    td = tr.insertCell(); td.innerHTML = '<button class="btn" style="padding:2px 8px;font-size:11px;" onclick="delL2(' + e.idx + ')">' + (t('l2_delete') || 'Delete') + '</button>';
    tbody.appendChild(tr);
  });
  l2Entries = [];
}

function delL2(idx) {
  fetchAPI('GET', '/l2_del.json?idx=' + idx, function() { notify('L2 entry deleted.', 'success'); });
}

function applyIGMP() {
  var en = document.getElementById('igmp-en').checked;
  fetchAPI('POST', '/cmd', function() { notify('IGMP ' + (en ? 'ON' : 'OFF'), 'success'); }, en ? 'igmp on' : 'igmp off');
}

/** STATS COUNTERS **/
var mib_counters = [
  'In Octets',8,'Out Octets',8,'In Unicast Pkts',8,'In Multicast Pkts',8,'In Broadcast Pkts',8,
  'Out Unicast Pkts',8,'Out Multicast Pkts',8,'Out Broadcast Pkts',8,'Out discards',4,
  '802.1d Tp Port discards',4,'802.3 Single collision',4,'802.3 Multi collision',4,
  '802.3 Deferred tx',4,'802.3 Late collisions',4,'802.3 Excessive collisions',4,
  '802.3 Symbol errors',4,'802.3 Control in unk',4,'802.3 In Pause frames',4,
  '802.3 Out Pause frames',4,'Ether drop events',4,'TX Broadcast Pkts',4,'TX Multicast Pkts',4,
  'TX CRC Align errors',4,'RX CRC Align errors',4,'TX Undersized Pkts',4,'RX Undersized Pkts',4,
  'TX Oversized Pkts',4,'RX Oversized Pkts',4,'TX Fragments',4,'RX fragments',4,'TX Jabbers',4,
  'RX Jabbers',4,'TX Collisions',4,'TX 640 Octets',4,'RX 640 Octets',4,'TX 65-127 Octets',4,
  'RX 65-127 Octets',4,'TX 128-255 Octets',4,'RX 128-255 Octets',4,'TX 256-511 Octets',4,
  'RX 256-511 Octets',4,'TX 512-1023 Octets',4,'RX 512-1023 Octets',4,'TX 1024-1518 Octets',4,
  'RX 1024-1518 Octets',4,'',4,'RX Undersized Drops',4,'TX >1518 Octets',4,'RX >1518 Octets',4,
  'TX Pkts too large',4,'RX Pkts too large',4,'TX Flex Octets S1',4,'RX Flex Octets S1',4,
  'TX Flex CRC S1',4,'RX Flex CRC S1',4,'TX Flex Octets S0',4,'RX Flex Octets S0',4,
  'TX Flex CRC S0',4,'RX Flex CRC S0',4,'Length Field Errors',4,'False Carriers',4,
  'Undersized Octets',4,'Framing Errors',4,'',4,'RX MAC Discards',4,'RX MAC IPG Short Drop',4,
  '',4,'802.1d TP Learned Discard',4,'EQ 7 Dropped Pkts',4,'EQ 6 Dropped Pkts',4,
  'EQ 5 Dropped Pkts',4,'EQ 4 Dropped Pkts',4,'EQ 3 Dropped Pkts',4,'EQ 2 Dropped Pkts',4,
  'EQ 1 Dropped Pkts',4,'EQ 0 Dropped Pkts',4,'EQ 7 Out Pkts',4,'EQ 6 Out Pkts',4,
  'EQ 5 Out Pkts',4,'EQ 4 Out Pkts',4,'EQ 3 Out Pkts',4,'EQ 2 Out Pkts',4,
  'EQ 1 Out Pkts',4,'EQ 0 Out Pkts',4,'TX Good Counter',8,'RX Good Counter',8,
  'RX Error Counter',4,'TX Error Counter',4,'TX Good PHY',8,'RX Good PHY',8,
  'RX Error PHY',4,'TX Error PHY',4
];

function showCounters(p) {
  var queryPort = physToLogMap[p] !== undefined ? physToLogMap[p] : (p - 1);
  document.getElementById('statsModalTitle').textContent = (t('stat_detailed') || 'Detailed Counters') + ': Port ' + p;
  document.getElementById('statsModal').style.display = 'flex';
  fetchAPI('GET', '/counters.json?port=' + queryPort, function(raw) {
    try {
      var s = JSON.parse(raw);
      var t = '<table><tbody><tr>';
      var c = 0;
      for (var i = 0; i < mib_counters.length; i += 2) {
        if (!mib_counters[i]) continue;
        var count = s[Math.floor(i / 2)] ? BigInt(s[Math.floor(i / 2)]) : 0n;
        t += '<td style="font-size:11px;">' + mib_counters[i] + '</td><td style="font-size:11px;font-weight:600;">' + (mib_counters[i+1] === 8 ? count.toString() : (count & 4294967295n).toString()) + '</td>';
        c++;
        if (c === 2) { t += '</tr><tr>'; c = 0; }
      }
      document.getElementById('statsModalBody').innerHTML = t + '</tr></tbody></table>';
    } catch (e) { document.getElementById('statsModalBody').innerHTML = 'Error loading counters.'; }
  });
}
function closeCounters() { document.getElementById('statsModal').style.display = 'none'; }

/** MIRROR **/
function loadMirrorConfig() {
  var txDiv = document.getElementById('mirror-tx-ports');
  var rxDiv = document.getElementById('mirror-rx-ports');
  if (!txDiv || !globalNumPorts) return;
  txDiv.innerHTML = '';
  rxDiv.innerHTML = '';
  for (var i = 1; i <= globalNumPorts; i++) {
    var d = document.createElement('div');
    d.style.cssText = 'display:inline-block;text-align:center;margin:4px;';
    var l = document.createElement('label');
    l.style.cssText = 'display:block;font-size:11px;';
    l.textContent = i;
    var inp = document.createElement('input');
    inp.type = 'checkbox';
    inp.id = 'mtx' + i;
    inp.style.cssText = 'margin:4px;transform:scale(1.2);';
    l.appendChild(inp);
    d.appendChild(l);
    txDiv.appendChild(d);
    var d2 = d.cloneNode(true);
    d2.children[0].children[0].id = 'mrx' + i;
    rxDiv.appendChild(d2);
  }
  fetchAPI('GET', '/mirror.json', function(raw) {
    try {
      var s = JSON.parse(raw);
      document.getElementById('mirror-en').checked = s.enabled;
      document.getElementById('mirror-port').value = s.mPort;
      var m_tx = parseInt(s.mirror_tx, 2);
      var m_rx = parseInt(s.mirror_rx, 2);
      for (var i = 1; i <= globalNumPorts; i++) {
        var bit = physToLogMap[i] !== undefined ? physToLogMap[i] : (i - 1);
        document.getElementById('mtx' + i).checked = (m_tx >> bit) & 1;
        document.getElementById('mrx' + i).checked = (m_rx >> bit) & 1;
      }
    } catch (e) {}
  });
}

function applyMirror() {
  var mp = document.getElementById('mirror-port').value;
  if (!mp) return notify('Set Mirroring Port first', 'warning');
  document.getElementById('mtx' + mp).checked = false;
  document.getElementById('mrx' + mp).checked = false;
  var cmd = 'mirror ' + mp;
  for (var i = 1; i <= globalNumPorts; i++) {
    var t = document.getElementById('mtx' + i).checked;
    var r = document.getElementById('mrx' + i).checked;
    if (t && r) cmd += ' ' + i;
    else if (t) cmd += ' ' + i + 't';
    else if (r) cmd += ' ' + i + 'r';
  }
  if (cmd.length < 10) return notify('Select Mirrored Ports', 'warning');
  fetchAPI('POST', '/cmd', function() { notify('Mirror applied.', 'success'); }, cmd);
}
function disableMirror() {
  fetchAPI('POST', '/cmd', function() { notify('Mirror disabled.', 'success'); }, 'mirror off');
}

/** LAG **/
function loadLagConfig() {
  fetchAPI('GET', '/lag.json', function(raw) {
    try {
      var s = JSON.parse(raw);
      var html = '';
      for (var l = 0; l < 4; l++) {
        var members = s[l] ? parseInt(s[l].members, 2) : 0;
        html += '<div class="lag-group"><h4>LAG Group ' + (l + 1) + '</h4><div style="display:flex;flex-wrap:wrap;gap:10px;margin-bottom:10px;">';
        for (var i = 1; i <= globalNumPorts; i++) {
          var bit = physToLogMap[i] !== undefined ? physToLogMap[i] : (i - 1);
          var checked = (members & (1 << bit)) ? 'checked' : '';
          html += '<label style="font-size:13px;cursor:pointer;"><input type="checkbox" id="lag' + l + '_p' + i + '" ' + checked + ' style="margin-right:4px;">P' + i + '</label>';
        }
        html += '</div><button class="btn" style="padding:6px 12px;font-size:12px;" onclick="applyLAG(' + l + ')">Save LAG ' + (l + 1) + '</button></div>';
      }
      document.getElementById('lag-container').innerHTML = html;
    } catch (e) {}
  });
}

function applyLAG(l) {
  var cmd = 'lag ' + (l + 1);
  for (var i = 1; i <= globalNumPorts; i++) {
    if (document.getElementById('lag' + l + '_p' + i).checked) cmd += ' ' + i;
  }
  fetchAPI('POST', '/cmd', function() { notify('LAG Group ' + (l + 1) + ' updated.', 'success'); }, cmd);
}

/** EEE **/
function loadEeeConfig() {
  var tbody = document.getElementById('eeebody');
  if (!tbody || !globalNumPorts) return;
  tbody.innerHTML = '';
  for (var i = 1; i <= globalNumPorts; i++) {
    var tr = document.createElement('tr');
    tr.id = 'eee-row-' + i;
    for (var j = 0; j < 8; j++) tr.appendChild(document.createElement('td'));
    tr.cells[0].innerHTML = '<strong>' + (t('common_port') || 'Port ') + i + '</strong>';
    tbody.appendChild(tr);
  }
  fetchAPI('GET', '/eee.json', function(raw) {
    try {
      var s = JSON.parse(raw);
      for (var i = 0; i < s.length; i++) {
        var p = s[i];
        var n = parseInt(p.portNum, 10);
        if (isNaN(n)) continue;
        var tr = document.getElementById('eee-row-' + n);
        if (!tr) continue;
        if (!p.isSFP) {
          var eee = parseInt(p.eee, 2);
          var lp = parseInt(p.eee_lp, 2);
          tr.cells[1].textContent = eee & 4 ? 'ON' : 'OFF';
          tr.cells[2].textContent = eee & 2 ? 'ON' : 'OFF';
          tr.cells[3].textContent = eee & 1 ? 'ON' : 'OFF';
          tr.cells[4].textContent = lp & 4 ? 'ON' : 'OFF';
          tr.cells[5].textContent = lp & 2 ? 'ON' : 'OFF';
          tr.cells[6].textContent = lp & 1 ? 'ON' : 'OFF';
          tr.cells[7].textContent = p.active;
        }
        tr.style.opacity = p.isSFP ? '0.4' : '1';
      }
    } catch (e) {}
  });
}

function eeeSub(port, enable) {
  fetchAPI('POST', '/cmd', function() { notify('EEE ' + (enable ? 'enabled' : 'disabled'), 'success'); }, 'eee' + (port ? ' ' + port : '') + ' ' + (enable ? 'on' : 'off'));
}

/** BANDWIDTH **/
function loadBwConfig() {
  fetchAPI('GET', '/bandwidth.json', function(raw) {
    try {
      var s = JSON.parse(raw);
      var tbody = document.getElementById('bw-body');
      tbody.innerHTML = '';
      s.forEach(function(p) {
        var portNum = parseInt(p.portNum, 10);
        if (isNaN(portNum)) return;
        var iBW = parseInt(p.iBW, 16) * 16;
        var eBW = parseInt(p.eBW, 16) * 16;
        var tr = document.createElement('tr');
        tr.id = 'bw-row-' + portNum;
        tr.innerHTML = '<td><strong>' + (t('common_port') || 'Port ') + portNum + '</strong></td>'
          + '<td class="cb-cell"><input type="checkbox" id="ilim_' + portNum + '" ' + (p.iLimited ? 'checked' : '') + ' onchange="toggleBW(' + portNum + ',\'i\')"></td>'
          + '<td id="td_ibw_' + portNum + '"><input type="number" id="ibw_' + portNum + '" style="width:80px;" value="' + (p.iLimited ? iBW : '') + '" placeholder="' + (p.iLimited ? '' : 'UNLIMITED') + '" ' + (p.iLimited ? '' : 'disabled') + ' oninput="document.getElementById(\'bwbtn_' + portNum + '\').disabled=false"></td>'
          + '<td class="cb-cell"><input type="checkbox" id="ifc_' + portNum + '" ' + (p.iFC ? 'checked' : '') + ' ' + (p.iLimited ? '' : 'disabled') + ' onchange="document.getElementById(\'bwbtn_' + portNum + '\').disabled=false"></td>'
          + '<td class="cb-cell"><input type="checkbox" id="elim_' + portNum + '" ' + (p.eLimited ? 'checked' : '') + ' onchange="toggleBW(' + portNum + ',\'e\')"></td>'
          + '<td id="td_ebw_' + portNum + '"><input type="number" id="ebw_' + portNum + '" style="width:80px;" value="' + (p.eLimited ? eBW : '') + '" placeholder="' + (p.eLimited ? '' : 'UNLIMITED') + '" ' + (p.eLimited ? '' : 'disabled') + ' oninput="document.getElementById(\'bwbtn_' + portNum + '\').disabled=false"></td>'
          + '<td><button class="btn" id="bwbtn_' + portNum + '" style="padding:4px 8px;font-size:11px;" onclick="applyBandwidth(' + portNum + ')" disabled>' + (t('bw_col_apply') || 'Apply') + '</button></td>';
        tbody.appendChild(tr);
      });
    } catch (e) {}
  });
}

function toggleBW(p, dir) {
  document.getElementById('bwbtn_' + p).disabled = false;
  if (dir === 'i') {
    var lim = document.getElementById('ilim_' + p).checked;
    document.getElementById('ibw_' + p).disabled = !lim;
    document.getElementById('ifc_' + p).disabled = !lim;
    if (!lim) { document.getElementById('ibw_' + p).value = ''; document.getElementById('ibw_' + p).placeholder = 'UNLIMITED'; }
    if (lim) document.getElementById('ifc_' + p).checked = true;
  } else {
    var lim = document.getElementById('elim_' + p).checked;
    document.getElementById('ebw_' + p).disabled = !lim;
    if (!lim) { document.getElementById('ebw_' + p).value = ''; document.getElementById('ebw_' + p).placeholder = 'UNLIMITED'; }
  }
}

function applyBandwidth(p) {
  var logPort = p - 1;
  if (document.getElementById('ilim_' + p).checked) {
    var val = parseInt(document.getElementById('ibw_' + p).value || 0);
    var hex = Math.floor(val / 16).toString(16).padStart(4, '0');
    fetchAPI('POST', '/cmd', null, 'bw in ' + logPort + ' ' + hex);
    if (!document.getElementById('ifc_' + p).checked) fetchAPI('POST', '/cmd', null, 'bw in ' + logPort + ' drop');
  } else {
    fetchAPI('POST', '/cmd', null, 'bw in ' + logPort + ' off');
  }
  if (document.getElementById('elim_' + p).checked) {
    var val2 = parseInt(document.getElementById('ebw_' + p).value || 0);
    var hex2 = Math.floor(val2 / 16).toString(16).padStart(4, '0');
    fetchAPI('POST', '/cmd', null, 'bw out ' + logPort + ' ' + hex2);
  } else {
    fetchAPI('POST', '/cmd', null, 'bw out ' + logPort + ' off');
  }
  document.getElementById('bwbtn_' + p).disabled = true;
  notify('Bandwidth for Port ' + p + ' applied.', 'success');
}

/** SYSTEM **/
function loadSysConfig() {
  fetchAPI('GET', '/config', function(raw) {
    var clean = raw.replace(/[^\x09\x0A\x0D\x20-\x7E]/g, '').trim();
    parseConf(clean);
    document.getElementById('config-window').value = clean;
  });
}

function applyIP() {
  var ip = document.getElementById('sys-ip').value;
  var nm = document.getElementById('sys-mask').value;
  var gw = document.getElementById('sys-gw').value;
  var ipv4 = /^(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]\d|\d)(?:\.(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]\d|\d)){3}$/;
  if (!ipv4.test(ip) || !ipv4.test(nm) || !ipv4.test(gw)) return notify('Invalid IPv4 address format.', 'error');
  fetchAPI('POST', '/cmd', function() { notify('IP updated temporarily.', 'success'); }, 'ip ' + ip);
  fetchAPI('POST', '/cmd', null, 'netmask ' + nm);
  fetchAPI('POST', '/cmd', null, 'gw ' + gw);
}

function rebootSwitch() {
  if (!confirm('Are you sure you want to reboot?')) return;
  fetchAPI('GET', '/reset', function() { notify('Switch rebooting...', 'info'); });
}

function openTab(evt, tabId) {
  var parent = evt.currentTarget.parentElement;
  parent.querySelectorAll('.tab-btn').forEach(function(b) { b.classList.remove('active'); });
  parent.parentElement.querySelectorAll('.tab-content').forEach(function(c) { c.classList.remove('active'); });
  evt.currentTarget.classList.add('active');
  document.getElementById(tabId).classList.add('active');
}

function sendConsoleCmd() {
  var cmd = document.getElementById('console-cmd').value;
  if (!cmd) return notify('Enter a command.', 'warning');
  fetchAPI('POST', '/cmd', function() { notify('Command sent.', 'success'); }, cmd);
}

function clearStartupConfig() {
  var lines = '';
  ['sys-ip','sys-mask','sys-gw'].forEach(function(id) {
    var el = document.getElementById(id);
    if (el) lines += { 'sys-ip': 'ip ', 'sys-mask': 'netmask ', 'sys-gw': 'gw ' }[id] + el.value + '\n';
  });
  document.getElementById('config-window').value = lines;
  notify('Startup config cleared (retained IP).', 'info');
}

/** CONFIG PARSER (getKey-based - isolates each config line type) **/
function getKey(line) {
  if (line.match(/^ip\s+/)) return 'ip';
  if (line.match(/^gw\s+/)) return 'gw';
  if (line.match(/^netmask\s+/)) return 'netmask';
  if (line.match(/^syslog\s+ip\s+/)) return 'syslog ip';
  if (line.match(/^syslog\s+/)) return 'syslog';
  if (line.match(/^passwd\s+/)) return 'passwd';
  if (line.match(/^eee\s+\d+/)) return line.match(/^eee\s+\d+/)[0];
  if (line.match(/^eee\s+(on|off)/)) return 'eee';
  if (line.match(/^ingress\s+/)) return line.match(/^ingress(\s+\d+)?/)[0];
  if (line.match(/^mirror\s+off/)) return 'mirror off';
  if (line.match(/^mirror\s+\d+/)) return line.match(/^mirror\s+\d+/)[0];
  if (line.match(/^vlan\s+\d+\s+mgmt/)) return line.match(/^vlan\s+\d+\s+mgmt/)[0];
  if (line.match(/^vlan\s+\d+\s+d/)) return line.match(/^vlan\s+\d+\s+d/)[0];
  if (line.match(/^vlan\s+\d+/)) return line.match(/^vlan\s+\d+/)[0];
  if (line.match(/^pvid\s+\d+/)) return line.match(/^pvid\s+\d+/)[0];
  if (line.match(/^lag\s+\d+/)) return line.match(/^lag\s+\d+/)[0];
  if (line.match(/^laghash\s+/)) return 'laghash';
  if (line.match(/^isolate\s+\d+/)) return line.match(/^isolate\s+\d+/)[0];
  if (line.match(/^stp\s+/)) return 'stp';
  if (line.match(/^igmp\s+/)) return 'igmp';
  if (line.match(/^bw\s+(in|out)\s+\d+/)) return line.match(/^bw\s+(in|out)\s+\d+/)[0];
  if (line.match(/^port\s+\d+\s+name/)) return line.match(/^port\s+\d+\s+name/)[0];
  if (line.match(/^port\s+\d+/)) return line.match(/^port\s+\d+/)[0];
  if (line.match(/^mtu\s+\d+/)) return line.match(/^mtu\s+\d+/)[0];
  return null;
}

function parseConf(s) {
  var clean = s.replace(/[^\x09\x0A\x0D\x20-\x7E]/g, '');
  var a = clean.split(/\r\n|\n/);
  for (var l = 0; l < a.length; l++) {
    var line = a[l].trim().replace(/\s+/g, ' ');
    if (!line.length) continue;
    var key = getKey(line);
    if (key) {
      configuration = configuration.filter(function(item) { return getKey(item) !== key; });
    } else {
      configuration = configuration.filter(function(item) { return item !== line; });
    }
    configuration.push(line);
  }
}

var isFlushing = false;

async function sendConfig(c) {
  var form = new FormData();
  form.append('MAX_FILE_SIZE', '4096');
  form.append('configuration', new Blob([c], {type: 'application/octet-stream'}), 'config.txt');
  try {
    await fetch('/config', { method: 'POST', body: form });
    notify('Configuration saved to flash!', 'success');
  } catch (err) {
    notify('Configuration saved! (Connection reset expected)', 'success');
  }
}

async function flashSaveConfig() {
  if (isFlushing) return;
  var btn = document.getElementById('saveBtn');
  isFlushing = true;
  notify('Merging and saving to flash...', 'info');
  try {
    configuration = [];
    var resConfig = await fetch('/config');
    if (resConfig.ok) parseConf(await resConfig.text());
    var resLog = await fetch('/cmd_log');
    if (resLog.ok) parseConf(await resLog.text());
    var body = configuration.join('\n') + '\n';
    await sendConfig(body);
    setTimeout(function() { fetch('/cmd_log_clear', { method: 'GET' }).catch(function() {}); }, 1000);
    document.getElementById('config-window').value = body;
  } catch (err) {} finally { isFlushing = false; }
}

function saveManualConfig() {
  var text = document.getElementById('config-window').value.replace(/[^\x09\x0A\x0D\x20-\x7E]/g, '');
  sendConfig(text).then(function() {
    setTimeout(function() { fetch('/cmd_log_clear', { method: 'GET' }).catch(function() {}); }, 1000);
  });
}

/** FIRMWARE UPDATE **/
function startFlash() {
  var file = document.getElementById('binFile').files[0];
  if (!file || !file.name.toLowerCase().endsWith('.bin')) return notify('Select a valid .bin file.', 'error');
  if (!confirm('WARNING: Flashing interrupts traffic. Do not power off. Proceed?')) return;

  isFlashing = true;
  clearInterval(poller); poller = null;
  clearInterval(systemInterval); systemInterval = null;
  clearInterval(l2Interval); l2Interval = null;
  reqQ = []; inFlight = null;

  var bar = document.getElementById('fBar');
  var text = document.getElementById('fText');
  var status = document.getElementById('fStatus');
  document.getElementById('flashBtn').disabled = true;
  document.getElementById('progress-wrap').style.display = 'block';

  status.innerText = 'UPLOADING FIRMWARE TO RAM...';
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/upload', true);
  var fNote = false;

  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      var p = Math.round((e.loaded / e.total) * 100);
      bar.style.width = Math.round(p * 0.2) + '%';
      text.innerText = p + '%';
      if (p === 100 && !fNote) {
        fNote = true;
        status.innerText = 'WRITING TO FLASH. DO NOT UNPLUG!';
        notify('Upload complete. Flashing memory...', 'warning');
        var fakeP = 20;
        var rebootTimer = setInterval(function() {
          if (++fakeP <= 99) { bar.style.width = fakeP + '%'; text.innerText = fakeP + '%'; }
          if (fakeP === 40) status.innerText = 'REBOOTING SWITCH... PLEASE WAIT.';
          if (fakeP === 100) {
            clearInterval(rebootTimer);
            status.innerText = 'REBOOT COMPLETE. RELOADING...';
            bar.style.background = 'var(--success)';
            setTimeout(function() { window.location.href = '/login.html'; }, 2000);
          }
        }, 600);
      }
    }
  };
  var fd = new FormData();
  fd.append('MAX_FILE_SIZE', '1000000');
  fd.append('uploadedfile', new Blob([file], {type: 'application/octet-stream'}), 'update.bin');
  xhr.send(fd);
}

/** SFP EEPROM **/
var sfpData = new Uint8Array(256);
var sfpSlot = 0;

function hex(b) { return (b >> 4).toString(16) + (b & 0xf).toString(16); }

function loadEeprom() {
  sfpSlot = parseInt(document.getElementById('slotsel').value);
  fetchAPI('GET', '/sfp_eeprom.json?slot=' + sfpSlot, function(raw) {
    try {
      var j = JSON.parse(raw);
      if (j.data) {
        for (var i = 0; i < 256; i++) sfpData[i] = parseInt(j.data.substr(i * 2, 2), 16);
        showEeprom();
        showSfpInfo();
      }
    } catch (e) { notify('Failed to load EEPROM.', 'error'); }
  });
}

function showEeprom() {
  var h = '<table style="border-collapse:collapse"><tr><th></th>';
  for (var c = 0; c < 16; c++) h += '<th style="width:24px;font-size:10px;color:#888">' + c.toString(16) + '</th>';
  h += '<th style="width:120px;font-size:10px;color:#888">ASCII</th></tr>';
  for (var r = 0; r < 16; r++) {
    h += '<tr><td style="font-size:10px;color:#888">' + hex(r << 4) + '</td>';
    var ascii = '';
    for (var c = 0; c < 16; c++) {
      var b = sfpData[r * 16 + c];
      h += '<td id="b' + (r * 16 + c) + '" style="border:1px solid #ddd;text-align:center;cursor:pointer;font-size:11px"';
      h += ' onclick="editByte(' + (r * 16 + c) + ')" title="Click to edit">' + hex(b) + '</td>';
      ascii += (b >= 32 && b < 127) ? String.fromCharCode(b) : '.';
    }
    h += '<td style="border:1px solid #ddd;padding-left:8px;font-size:11px;color:#666">' + ascii + '</td></tr>';
  }
  h += '</table>';
  document.getElementById('hexdump').innerHTML = h;
  document.getElementById('hexdump').style.display = 'block';
}

function showSfpInfo() {
  function readStr(start, end) {
    var s = '';
    for (var i = start; i < end; i++) s += String.fromCharCode(sfpData[i]);
    return s.replace(/\0/g, '').trim();
  }
  var v = readStr(20, 36), pn = readStr(40, 56), sn = readStr(68, 84);
  document.getElementById('info').innerHTML = '<b>Vendor:</b> ' + v + ' | <b>PN:</b> ' + pn + ' | <b>SN:</b> ' + sn + ' | <b>Type:</b> 0x' + hex(sfpData[3]);
}

function editByte(offset) {
  var cell = document.getElementById('b' + offset);
  var oldVal = sfpData[offset];
  var newVal = prompt('Edit byte 0x' + hex(offset) + ' (0x00-0xFF):', hex(oldVal));
  if (newVal === null) return;
  if (newVal.startsWith('0x')) newVal = newVal.substring(2);
  var v = parseInt(newVal, 16);
  if (isNaN(v) || v < 0 || v > 255) { notify('Invalid value', 'error'); return; }
  var pw = pwArg();
  fetchAPI('POST', '/cmd', function() {
    sfpData[offset] = v;
    cell.textContent = hex(v);
    cell.style.backgroundColor = '#ff8';
    setTimeout(function() { cell.style.backgroundColor = ''; }, 2000);
    notify('Byte 0x' + hex(offset) + ' updated.', 'success');
  }, 'sfp ' + (sfpSlot + 1) + ' write ' + hex(offset) + ' ' + hex(v) + pw);
}

function pwArg() {
  var pw = document.getElementById('pwinput').value.trim();
  if (!pw) return '';
  if (!/^[0-9a-fA-F]{8}$/.test(pw)) { notify('Password must be 8 hex digits.', 'error'); return ''; }
  return ' --pw ' + pw;
}

function patchEeprom() {
  if (!confirm('Patch SFP ' + (sfpSlot + 1) + ' EEPROM (FC→Ethernet)?')) return;
  fetchAPI('POST', '/cmd', function() { notify('Patch complete.', 'success'); loadEeprom(); }, 'sfp ' + (sfpSlot + 1) + ' patch' + pwArg());
}

function fixChecksum() {
  if (!confirm('Fix checksums on SFP ' + (sfpSlot + 1) + '?')) return;
  fetchAPI('POST', '/cmd', function() { notify('Checksums fixed.', 'success'); loadEeprom(); }, 'sfp ' + (sfpSlot + 1) + ' checksum --fix' + pwArg());
}

function saveBackup() {
  if (!confirm('Save current SFP EEPROM to flash backup?')) return;
  fetchAPI('POST', '/cmd', function() { notify('Saved to flash.', 'success'); }, 'sfp ' + (sfpSlot + 1) + ' save');
}

function restoreBackup() {
  if (!confirm('Restore EEPROM from flash backup?')) return;
  fetchAPI('POST', '/cmd', function() { notify('Restored.', 'success'); loadEeprom(); }, 'sfp ' + (sfpSlot + 1) + ' restore');
}

function downloadBin() {
  function readStr(start, end) {
    var s = '';
    for (var i = start; i < end; i++) s += String.fromCharCode(sfpData[i]);
    return s.replace(/\0/g, '').trim().replace(/\s+/g, '_');
  }
  var vendor = readStr(20, 36);
  var pn = readStr(40, 56);
  var name = (vendor ? vendor + '_' : '') + (pn ? pn : 'sfp' + (sfpSlot + 1) + '_eeprom') + '.bin';
  var buf = new ArrayBuffer(256);
  var view = new Uint8Array(buf);
  for (var i = 0; i < 256; i++) view[i] = sfpData[i];
  var blob = new Blob([buf], {type: 'application/octet-stream'});
  var url = URL.createObjectURL(blob);
  var a = document.createElement('a');
  a.href = url; a.download = name; a.click();
  URL.revokeObjectURL(url);
}

function uploadBin(input) {
  var file = input.files[0];
  if (!file) return;
  if (file.size != 256) { notify('File must be exactly 256 bytes.', 'error'); return; }
  if (!confirm('Write ' + file.name + ' to SFP ' + (sfpSlot + 1) + ' EEPROM?')) return;
  var reader = new FileReader();
  reader.onload = function(e) {
    var data = new Uint8Array(e.target.result);
    var hexStr = '';
    for (var i = 0; i < 256; i++) hexStr += hex(data[i]);
    fetchAPI('POST', '/cmd', function() { notify('Write complete.', 'success'); loadEeprom(); }, 'sfp ' + (sfpSlot + 1) + ' bulk ' + hexStr + pwArg());
  };
  reader.readAsArrayBuffer(file);
}

/** INIT **/
function initI18n() {
  document.querySelectorAll('[data-i18n]').forEach(function(el) {
    if (typeof t === 'function') {
      var key = el.getAttribute('data-i18n');
      if (el.tagName === 'INPUT' && (el.type === 'submit' || el.type === 'button')) el.value = t(key);
      else if (el.tagName === 'OPTION') el.textContent = t(key);
      else if (el.tagName === 'TITLE') el.textContent = t(key);
      else el.innerHTML = t(key);
    }
  });
}

window.addEventListener('load', function() {
  initI18n();
  nav('dash');
  document.getElementById('vlanSelect').onchange = function() {
    document.getElementById('vid').value = this.value;
    fetchVLAN();
  };
});
