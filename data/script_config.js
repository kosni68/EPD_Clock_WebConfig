function setSelectValueOrAdd(selectEl, value) {
  if (!selectEl) return;
  const target = value || '';
  const exists = Array.from(selectEl.options).some(opt => opt.value === target);
  if (!exists && target.length) {
    const opt = document.createElement('option');
    opt.value = target;
    opt.textContent = `${target} (custom)`;
    selectEl.appendChild(opt);
  }
  selectEl.value = target;
}

async function fetchConfig() {
  try {
    const res = await fetch('/api/config', {cache: 'no-store'});
    if (!res.ok) {
      showStatus('Config load error: ' + res.status, true);
      return;
    }
    const json = await res.json();

    document.getElementById('wifi_ssid').value = json.wifi_ssid || '';

    document.getElementById('mqtt_enabled').checked = json.mqtt_enabled === true;
    document.getElementById('mqtt_host').value = json.mqtt_host || '';
    document.getElementById('mqtt_port').value = json.mqtt_port || 1883;
    document.getElementById('mqtt_user').value = json.mqtt_user || '';
    document.getElementById('mqtt_topic').value = json.mqtt_topic || '';

    document.getElementById('interactive_timeout_ms').value = json.interactive_timeout_ms || 600000;
    document.getElementById('deepsleep_interval_s').value = json.deepsleep_interval_s || 60;

    document.getElementById('device_name').value = json.device_name || '';
    document.getElementById('admin_user').value = json.admin_user || '';

    // sensor offsets
    if (typeof json.temp_offset_c !== 'undefined')
      document.getElementById('temp_offset').value = json.temp_offset_c;
    else
      document.getElementById('temp_offset').value = 0;
    if (typeof json.hum_offset_pct !== 'undefined')
      document.getElementById('hum_offset').value = json.hum_offset_pct;
    else
      document.getElementById('hum_offset').value = 0;

    setSelectValueOrAdd(
      document.getElementById('tz_string'),
      json.tz_string || 'CET-1CEST,M3.5.0/2,M10.5.0/3'
    );

    showStatus('Config loaded', false);
  } catch (e) {
    showStatus('Fetch error: ' + e, true);
  }
}

function gatherConfig() {
  const obj = {};

  obj.wifi_ssid = document.getElementById('wifi_ssid').value || '';
  const wpass = document.getElementById('wifi_pass').value;
  if (wpass && wpass.length > 0) obj.wifi_pass = wpass;

  obj.mqtt_enabled = document.getElementById('mqtt_enabled').checked;
  obj.mqtt_host = document.getElementById('mqtt_host').value;
  obj.mqtt_port = parseInt(document.getElementById('mqtt_port').value) || 1883;
  obj.mqtt_user = document.getElementById('mqtt_user').value;
  const mp = document.getElementById('mqtt_pass').value;
  if (mp && mp.length > 0) obj.mqtt_pass = mp;
  obj.mqtt_topic = document.getElementById('mqtt_topic').value;

  obj.interactive_timeout_ms = parseInt(document.getElementById('interactive_timeout_ms').value) || 600000;
  obj.deepsleep_interval_s = parseInt(document.getElementById('deepsleep_interval_s').value) || 60;

  obj.device_name = document.getElementById('device_name').value || '';
  obj.admin_user = document.getElementById('admin_user').value || '';
  const ap = document.getElementById('admin_pass').value;
  if (ap && ap.length > 0) obj.admin_pass = ap;

  obj.tz_string = document.getElementById('tz_string').value || '';

  // sensor offsets
  const to = parseFloat(document.getElementById('temp_offset').value);
  obj.temp_offset_c = isNaN(to) ? 0.0 : to;
  const ho = parseFloat(document.getElementById('hum_offset').value);
  obj.hum_offset_pct = isNaN(ho) ? 0.0 : ho;

  return obj;
}

async function saveConfig() {
  const payload = gatherConfig();
  try {
    const res = await fetch('/api/config', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(payload)
    });
    const j = await res.json();
    if (res.ok && j.ok) {
      showStatus('Config saved', false);
      document.getElementById('admin_pass').value = '';
      document.getElementById('wifi_pass').value = '';
      document.getElementById('mqtt_pass').value = '';
    } else {
      showStatus('Save error', true);
    }
  } catch (e) {
    showStatus('POST error: ' + e, true);
  }
}

function showStatus(msg, isError) {
  const el = document.getElementById('status');
  el.innerText = msg;
  el.style.color = isError ? 'red' : 'green';
}

document.getElementById('btnSave').addEventListener('click', () => {
  saveConfig();
});

document.getElementById('btnReload').addEventListener('click', () => {
  fetchConfig();
});

async function rebootDevice() {
  if (!confirm('Are you sure you want to reboot the device?')) {
    return;
  }
  try {
    const res = await fetch('/api/reboot', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'}
    });
    if (res.ok) {
      showStatus('Rebooting...', false);
    } else {
      showStatus('Reboot error: ' + res.status, true);
    }
  } catch (e) {
    showStatus('Error: ' + e, true);
  }
}

document.getElementById('btnReboot').addEventListener('click', () => {
  rebootDevice();
});

document.getElementById('btnMqttTest').addEventListener('click', async () => {
  showStatus('Testing MQTT...', false);
  try {
    const res = await fetch('/api/mqtt/test', {method: 'POST'});
    if (res.ok) {
      showStatus('MQTT test succeeded', false);
    } else {
      showStatus('MQTT test failed: ' + res.status, true);
    }
  } catch (e) {
    showStatus('MQTT error: ' + e, true);
  }
});

// Wi-Fi scan + selection
async function scanWifi() {
  const btn = document.getElementById('btnScanWifi');
  const select = document.getElementById('wifi_scan_list');
  const status = document.getElementById('wifi_scan_status');
  if (!btn || !select) return;
  btn.disabled = true;
  if (status) {
    status.innerText = 'Scanning...';
    status.style.color = '#555';
  }
  try {
    const res = await fetch('/api/wifi/scan');
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const j = await res.json();
    if (!j.ok || !Array.isArray(j.aps)) throw new Error('Unexpected response');

    // Keep strongest entry per SSID
    const bestBySsid = {};
    j.aps.forEach(ap => {
      if (!ap || !ap.ssid) return;
      const key = ap.ssid;
      if (!bestBySsid[key] || (ap.rssi || -999) > (bestBySsid[key].rssi || -999)) {
        bestBySsid[key] = ap;
      }
    });
    const aps = Object.values(bestBySsid).sort((a, b) => (b.rssi || 0) - (a.rssi || 0));

    select.innerHTML = '<option value=\"\">-- select a scanned network --</option>';
    aps.forEach(ap => {
      const opt = document.createElement('option');
      opt.value = ap.ssid;
      opt.textContent = `${ap.ssid} (${ap.rssi || '?'} dBm)`;
      select.appendChild(opt);
    });
    if (status) {
      status.innerText = aps.length ? `${aps.length} network(s) found` : 'No networks found';
      status.style.color = aps.length ? 'green' : 'red';
    }
  } catch (e) {
    if (status) {
      status.innerText = 'Scan failed: ' + e.message;
      status.style.color = 'red';
    }
  } finally {
    btn.disabled = false;
  }
}

document.getElementById('btnScanWifi').addEventListener('click', scanWifi);
document.getElementById('wifi_scan_list').addEventListener('change', (e) => {
  const ssid = e.target.value || '';
  if (ssid.length) {
    document.getElementById('wifi_ssid').value = ssid;
  }
});

// Auto-refresh logs and act as ping using the unified /api/dashboard endpoint
async function refreshLogsAndPing() {
  const pre = document.getElementById('logOutput');
  try {
    const res = await fetch('/api/dashboard');
    if (res.ok) {
      const j = await res.json();
      if (j && j.ok) {
        if (pre && j.logs !== undefined) {
          // Only update if content changed (reduce flickering)
          if (pre.innerText !== j.logs) {
            pre.innerText = j.logs;
            pre.scrollTop = pre.scrollHeight;
          }
        }
        // Optionally reflect ping status briefly
        const statusEl = document.getElementById('status');
        if (statusEl) {
          statusEl.innerText = 'Device reachable';
          statusEl.style.color = 'green';
          setTimeout(() => { statusEl.innerText = ''; }, 1500);
        }
      }
    }
  } catch (e) {
    // silently ignore fetch errors during auto-refresh
  }
}

setInterval(refreshLogsAndPing, 2000);
refreshLogsAndPing(); // initial call

fetchConfig();
