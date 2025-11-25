async function fetchConfig() {
  try {
    const res = await fetch('/api/config', {cache: 'no-store'});
    if (!res.ok) {
      showStatus('Erreur chargement config: ' + res.status, true);
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

    document.getElementById('tz_string').value =
      json.tz_string || 'CET-1CEST,M3.5.0/2,M10.5.0/3';

    showStatus('Config chargée', false);
  } catch (e) {
    showStatus('Erreur fetch: ' + e, true);
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
      showStatus('Config sauvegardée', false);
      document.getElementById('admin_pass').value = '';
      document.getElementById('wifi_pass').value = '';
      document.getElementById('mqtt_pass').value = '';
    } else {
      showStatus('Erreur sauvegarde', true);
    }
  } catch (e) {
    showStatus('Erreur POST: ' + e, true);
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
  if (!confirm('Êtes-vous sûr de vouloir redémarrer l\'appareil ?')) {
    return;
  }
  try {
    const res = await fetch('/api/reboot', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'}
    });
    if (res.ok) {
      showStatus('Redémarrage en cours...', false);
    } else {
      showStatus('Erreur redémarrage: ' + res.status, true);
    }
  } catch (e) {
    showStatus('Erreur: ' + e, true);
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
      showStatus('MQTT test réussi', false);
    } else {
      showStatus('MQTT test échoué: ' + res.status, true);
    }
  } catch (e) {
    showStatus('Erreur MQTT: ' + e, true);
  }
});

// Auto-refresh logs every 2 seconds
async function refreshLogs() {
  const pre = document.getElementById('logOutput');
  try {
    const res = await fetch('/api/logs');
    if (res.ok) {
      const txt = await res.text();
      // Only update if content changed (reduce flickering)
      if (pre.innerText !== txt) {
        pre.innerText = txt;
        // Auto-scroll to bottom
        pre.scrollTop = pre.scrollHeight;
      }
    }
  } catch (e) {
    // silently ignore fetch errors during auto-refresh
  }
}

setInterval(refreshLogs, 2000);
refreshLogs(); // initial call

function sendConfigPing() {
  fetch('/ping', {
    method: 'POST',
    body: new URLSearchParams({page: 'config'})
  });
}
setInterval(sendConfigPing, 10000);

fetchConfig();
