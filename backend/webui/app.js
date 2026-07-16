/* AirCare custom control UI.
 *
 * Connects to HiveMQ Cloud over MQTT via WebSocket (TLS, port 8884) and
 * publishes the same command contract the ESP32 firmware expects on
 * AirCare/inCommands/<MAC> (or /broadcast). Grafana panels are embedded.
 *
 * Broker/user/pass are imported from config.js so they can be templated by
 * the Docker deployment without editing this file.
 */
(function () {
  "use strict";

  // ---- Deployment config (overridden by config.js in Docker) ----
  var CONFIG = window.AIRCARE_CONFIG || {
    broker: "4532efaca6c5432ea63dbae9a7690ef4.s1.eu.hivemq.cloud",
    wsPort: 8884,
    user: "aircaredevice",
    pass: "aircarepassword",
    grafanaBase: "http://localhost:3001",
    grafanaDashboardUid: "aircare-main",
    configJsonUrl: "config.json"
  };

  var client = null;
  var $ = function (id) { return document.getElementById(id); };

  function setConn(online) {
    var el = $("conn");
    el.className = "conn " + (online ? "online" : "offline");
    el.textContent = online ? "connected" : "disconnected";
  }

  function setStatus(msg, kind) {
    var el = $("status");
    el.textContent = msg;
    el.className = "status" + (kind ? " " + kind : "");
  }

  function connect() {
    var url = "wss://" + CONFIG.broker + ":" + CONFIG.wsPort + "/mqtt";
    setStatus("Connecting to " + CONFIG.broker + " …");
    client = mqtt.connect(url, {
      username: CONFIG.user,
      password: CONFIG.pass,
      reconnectPeriod: 3000,
      connectTimeout: 15000,
      clientId: "aircare-webui-" + Math.random().toString(16).slice(2, 10)
    });

    client.on("connect", function () {
      setConn(true);
      setStatus("Connected.", "ok");
    });
    client.on("reconnect", function () { setConn(false); setStatus("Reconnecting …"); });
    client.on("error", function (e) { setConn(false); setStatus("Error: " + e.message, "err"); });
    client.on("close", function () { setConn(false); setStatus("Disconnected."); });
  }

  function currentTarget() {
    if ($("broadcast").checked) return "AirCare/inCommands/broadcast";
    var mac = $("device").value;
    if (!mac) { setStatus("Select a device or enable broadcast.", "err"); return null; }
    return "AirCare/inCommands/" + mac;
  }

  function publish(payload) {
    var topic = currentTarget();
    if (!topic || !client) return;
    client.publish(topic, JSON.stringify(payload), { qos: 0 }, function (err) {
      if (err) setStatus("Publish failed: " + err.message, "err");
      else setStatus("Sent " + JSON.stringify(payload) + " → " + topic, "ok");
    });
  }

  function localTo8601(value) {
    // datetime-local "YYYY-MM-DDTHH:MM" → "YYYY-MM-DD HH:MM:SS" (device parser
    // expects the same format sched::parseDate uses).
    if (!value) return "";
    return value.replace("T", " ") + ":00";
  }

  function loadDevices() {
    fetch(CONFIG.configJsonUrl)
      .then(function (r) { return r.json(); })
      .then(function (cfg) {
        var sel = $("device");
        (cfg.Devices || []).forEach(function (d) {
          var o = document.createElement("option");
          o.value = d.Device;
          o.textContent = d.Label + "  (" + d.Device + ")";
          sel.appendChild(o);
        });
      })
      .catch(function (e) { setStatus("Could not load device list: " + e.message, "err"); });
  }

  function embedGrafana() {
    var src = CONFIG.grafanaBase + "/d/" + CONFIG.grafanaDashboardUid +
      "?kiosk&theme=dark&from=now-6h&to=now";
    $("grafana").src = src;
  }

  function wireControls() {
    document.querySelectorAll("button[data-cmd]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        var cmd = btn.getAttribute("data-cmd");
        var payload = { cmd: cmd };
        var val = btn.getAttribute("data-val");
        if (val) payload.value = val;
        if (cmd === "EXCEPTION") {
          payload.from = localTo8601($("exc_from").value);
          payload.to = localTo8601($("exc_to").value);
          payload.state = $("exc_state").checked ? "on" : "off";
          if (!payload.from || !payload.to) {
            setStatus("Set both From and To dates for an exception.", "err");
            return;
          }
        }
        publish(payload);
      });
    });
  }

  document.addEventListener("DOMContentLoaded", function () {
    loadDevices();
    embedGrafana();
    wireControls();
    connect();
  });
})();
