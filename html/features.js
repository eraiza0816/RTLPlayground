// Tier 1-3 WebUI feature scripts (loaded after main.js).
// Kept in a separate file because the httpd truncates any file > 64 KB
// (file length fields are 16-bit) and main.js is close to the limit.

/** Send a chain of CLI commands sequentially (the /cmd endpoint has no
 *  response body, so each step is only acknowledged). */
function sendCmdChain(cmds, doneMsg) {
  if (!cmds.length) { notify(doneMsg, 'success'); return; }
  var i = 0;
  function next() {
    if (i >= cmds.length) { notify(doneMsg, 'success'); return; }
    fetchAPI('POST', '/cmd', next, cmds[i++]);
  }
  next();
}

/** L2 panel: IGMP querier + MLD snooping toggles, group table **/
function loadIgmpState() {
  fetchAPI('GET', '/igmp.json', function(raw) {
    try {
      var j = JSON.parse(raw);
      $in('querier-en').checked = j.querier === 1;
      $in('mld-en').checked = j.mld_en === 1;
      var el = document.getElementById('igmp-groups');
      if (!el) return;
      if (j.groups && j.groups.length) {
        var html = '<b>Multicast groups:</b> ';
        j.groups.forEach(function(g) { html += 'idx ' + g.idx + ' (ports 0x' + g.pmask.toString(16) + ') '; });
        el.innerHTML = html;
      } else {
        el.innerHTML = '';
      }
    } catch (e) {}
  });
}

function applyQuerier() {
  var en = $in('querier-en').checked;
  fetchAPI('POST', '/cmd', function() { notify('IGMP querier ' + (en ? 'ON' : 'OFF'), 'success'); setTimeout(loadIgmpState, 300); },
    'igmp querier ' + (en ? 'on' : 'off'));
}

function applyMLD() {
  var en = $in('mld-en').checked;
  fetchAPI('POST', '/cmd', function() { notify('MLD snooping ' + (en ? 'ON' : 'OFF'), 'success'); setTimeout(loadIgmpState, 300); },
    'igmp mld ' + (en ? 'on' : 'off'));
}

/** QoS panel **/
function loadQos() {
  fetchAPI('GET', '/qos.json', function(raw) {
    try {
      var j = JSON.parse(raw);
      $in('qos-mode').value = '' + j.mode;
      var pcpEl = document.getElementById('qos-pcp-table');
      if (pcpEl) {
        var h = '<thead><tr><th>PCP</th>';
        for (var p = 0; p < 8; p++) h += '<th>' + p + '</th>';
        h += '</tr></thead><tbody><tr><th>Queue</th>';
        for (p = 0; p < 8; p++) {
          h += '<td><select id="qos-pcp-' + p + '">';
          for (var q = 0; q < 8; q++) h += '<option value="' + q + '"' + (j.pcp[p] === q ? ' selected' : '') + '>' + q + '</option>';
          h += '</select></td>';
        }
        h += '</tr></tbody>';
        pcpEl.innerHTML = h;
      }
      var dscpEl = document.getElementById('qos-dscp-table');
      if (dscpEl) {
        h = '<thead><tr><th>DSCP</th>';
        for (p = 0; p < 8; p++) h += '<th>' + (p * 8) + '-' + (p * 8 + 7) + '</th>';
        h += '</tr></thead><tbody><tr><th>Queue</th>';
        for (p = 0; p < 8; p++) {
          h += '<td><select id="qos-dscp-' + p + '">';
          for (q = 0; q < 8; q++) h += '<option value="' + q + '"' + (j.dscp[p * 8] === q ? ' selected' : '') + '>' + q + '</option>';
          h += '</select></td>';
        }
        h += '</tr></tbody>';
        dscpEl.innerHTML = h;
      }
      var schedEl = document.getElementById('qos-sched');
      if (schedEl) {
        var sh = '';
        j.sched.forEach(function(s, i) { sh += 'Port ' + (i + 1) + ': ' + s + '<br>'; });
        schedEl.innerHTML = sh;
      }
    } catch (e) {}
  });
}

function applyQosMode() {
  var m = $in('qos-mode').value;
  var mode = m === '0' ? 'off' : (m === '1' ? 'pcp' : (m === '2' ? 'dscp' : 'both'));
  fetchAPI('POST', '/cmd', function() { notify('QoS mode: ' + mode, 'success'); setTimeout(loadQos, 300); },
    m === '0' ? 'qos off' : 'qos mode ' + mode);
}

function applyQosPcp() {
  var cmds = [];
  for (var p = 0; p < 8; p++) cmds.push('qos pcp ' + p + ' ' + $in('qos-pcp-' + p).value);
  sendCmdChain(cmds, 'PCP map applied.');
}

function applyQosDscp() {
  var cmds = [];
  for (var p = 0; p < 8; p++) {
    var q = $in('qos-dscp-' + p).value;
    for (var d = 0; d < 8; d++) cmds.push('qos dscp ' + (p * 8 + d) + ' ' + q);
  }
  sendCmdChain(cmds, 'DSCP map applied.');
}

/** Storm control panel **/
var stormTypes = ['broadcast', 'multicast', 'dlf', 'unknown-mcast'];

function loadStorm() {
  fetchAPI('GET', '/storm-control.json', function(raw) {
    try {
      var j = JSON.parse(raw);
      var body = document.getElementById('storm-body');
      if (!body) return;
      var h = '';
      j.forEach(function(s) {
        var rate = parseInt(s.rate, 16) || 0;
        var id = 'storm-' + s.type;
        h += '<tr><td>' + stormTypes[s.type] + '</td>' +
          '<td><input type="checkbox" id="' + id + '-en" style="transform:scale(1.3);"' + (s.en ? ' checked' : '') + '></td>' +
          '<td><input type="number" id="' + id + '-rate" min="1" max="10000000" value="' + (rate > 0 ? rate : '') + '" style="width:120px;"></td>' +
          '<td><select id="' + id + '-unit" style="width:80px;"><option value="k" ' + (!s.pps ? 'selected' : '') + '>kbps</option><option value="p" ' + (s.pps ? 'selected' : '') + '>pps</option></select></td>' +
          '<td><button class="btn" style="padding:4px 10px;font-size:11px;" onclick="applyStorm(' + s.type + ')">Apply</button></td></tr>';
      });
      body.innerHTML = h;
    } catch (e) {}
  });
}

function applyStorm(type) {
  var id = 'storm-' + type;
  var en = $in(id + '-en').checked;
  var rate = $in(id + '-rate').value;
  var unit = $in(id + '-unit').value;
  var cmd;
  if (en) {
    if (!rate || parseInt(rate) < 1) return notify('Enter a rate.', 'warning');
    cmd = 'storm-control on ' + stormTypes[type] + ' ' + rate + unit;
  } else {
    cmd = 'storm-control off ' + stormTypes[type];
  }
  fetchAPI('POST', '/cmd', function() { notify('Storm control updated.', 'success'); setTimeout(loadStorm, 300); }, cmd);
}

/** ACL panel **/
function aclMatchFromWord(word, tpl) {
  if (tpl === 1) {
    var d1 = parseInt(word.data1, 16);
    return (d1 >>> 24) + '.' + ((d1 >>> 16) & 0xff) + '.' + ((d1 >>> 8) & 0xff) + '.' + (d1 & 0xff);
  }
  if (tpl === 4) return 'vlan ' + ((parseInt(word.data1, 16) >>> 16) & 0xfff);
  var d0 = parseInt(word.data0, 16), d = parseInt(word.data1, 16);
  function b(v) { return ('0' + (v & 0xff).toString(16)).slice(-2); }
  return b(d >>> 8) + ':' + b(d) + ':' + b(d0 >>> 24) + ':' + b(d0 >>> 16) + ':' + b(d0 >>> 8) + ':' + b(d0);
}

function loadAcl() {
  fetchAPI('GET', '/acl.json', function(raw) {
    try {
      var j = JSON.parse(raw);
      var body = document.getElementById('acl-body');
      if (!body) return;
      if (!j.length) { body.innerHTML = '<tr><td colspan="5" style="text-align:center;">No rules.</td></tr>'; return; }
      var h = '';
      j.forEach(function(r) {
        h += '<tr><td>' + r.idx + '</td><td>0x' + r.pmask.toString(16) + '</td><td>' + (r.action ? 'deny' : 'permit') +
          '</td><td>' + aclMatchFromWord(r, r.tpl) + '</td>' +
          '<td><button class="btn btn-danger" style="padding:4px 10px;font-size:11px;" onclick="aclDel(' + r.idx + ')">Delete</button></td></tr>';
      });
      body.innerHTML = h;
    } catch (e) {}
  });
}

function aclAdd() {
  var port = $in('acl-port').value;
  var action = $in('acl-action').value;
  var mtype = $in('acl-match-type').value;
  var match = $in('acl-match').value.trim();
  if (!port || port < 1 || port > 9) return notify('Enter a port 1-9.', 'warning');
  if (!match) return notify('Enter a match value.', 'warning');
  fetchAPI('POST', '/cmd', function() { notify('ACL rule added.', 'success'); setTimeout(loadAcl, 300); },
    'acl add ' + port + ' ' + action + ' ' + mtype + ' ' + match);
}

function aclDel(idx) {
  if (!confirm('Delete ACL rule ' + idx + '?')) return;
  fetchAPI('POST', '/cmd', function() { notify('ACL rule deleted.', 'success'); setTimeout(loadAcl, 300); }, 'acl del ' + idx);
}

/** System > Console: ping + ARP + running config **/
function runPing() {
  var ip = $in('ping-ip').value.trim();
  if (!ip) return notify('Enter an IP address.', 'warning');
  var parts = ip.split('.');
  if (parts.length !== 4) return notify('Enter a dotted-quad IP.', 'warning');
  for (var i = 0; i < 4; i++) if (!/^\d+$/.test(parts[i]) || +parts[i] > 255) return notify('Invalid IP.', 'warning');
  var res = document.getElementById('ping-result');
  res.textContent = 'pinging ' + ip + '...';
  fetchAPI('POST', '/cmd', function() {}, 'ping ' + ip);
  var tries = 0;
  var poll = setInterval(function() {
    fetchAPI('GET', '/ping.json', function(raw) {
      try {
        var j = JSON.parse(raw);
        if (j.state === 0) {
          clearInterval(poll);
          if (j.rcvd > 0) {
            res.textContent = j.dst + ': ' + j.sent + '/' + j.rcvd + ' received, rtt ' +
              j.min_rtt + '/' + (j.rcvd ? Math.round(j.sum_rtt / j.rcvd) : 0) + '/' + j.max_rtt + ' ms';
          } else {
            res.textContent = j.dst + ': no replies (sent ' + j.sent + ')';
          }
        } else {
          res.textContent = 'pinging ' + j.dst + '... (' + j.sent + '/' + j.rcvd + ')';
        }
      } catch (e) {}
    });
    if (++tries > 30) clearInterval(poll);
  }, 1000);
}

function loadArp() {
  fetchAPI('GET', '/arp.json', function(raw) {
    try {
      var j = JSON.parse(raw);
      var body = document.getElementById('arp-body');
      if (!body) return;
      if (!j.length) { body.innerHTML = '<tr><td colspan="3" style="text-align:center;">No ARP entries.</td></tr>'; return; }
      var h = '';
      j.forEach(function(e) { h += '<tr><td>' + e.ip + '</td><td>' + e.mac + '</td><td>' + e.age + '</td></tr>'; });
      body.innerHTML = h;
    } catch (e) {}
  });
}

function refreshRunningConfig() {
  fetchAPI('GET', '/running-config', function(raw) {
    var el = document.getElementById('running-config');
    if (el) el.textContent = raw;
  });
}

/** Hook into nav() so the new panels load their data when opened **/
var featuresBaseNav = window.nav;
window.nav = function(id) {
  featuresBaseNav(id);
  if (id === 'l2') loadIgmpState();
  else if (id === 'qos') loadQos();
  else if (id === 'storm') loadStorm();
  else if (id === 'acl') loadAcl();
  else if (id === 'sys') { loadArp(); refreshRunningConfig(); }
};
