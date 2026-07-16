#!/bin/sh
# Templates config.js from environment variables so broker/credentials/Grafana
# base URL can be injected at container start (no secrets baked into the image).
set -e

CONFIG_JS="/usr/share/nginx/html/config.js"

cat > "$CONFIG_JS" <<EOF
window.AIRCARE_CONFIG = {
  broker: "${MQTT_BROKER:-4532efaca6c5432ea63dbae9a7690ef4.s1.eu.hivemq.cloud}",
  wsPort: ${MQTT_WS_PORT:-8884},
  user: "${MQTT_USER:-aircaredevice}",
  pass: "${MQTT_PASS:-aircarepassword}",
  grafanaBase: "${GRAFANA_BASE:-http://localhost:3001}",
  grafanaDashboardUid: "${GRAFANA_DASHBOARD_UID:-aircare-main}",
  configJsonUrl: "config.json"
};
EOF

echo "wrote config.js from env"
exec nginx -g 'daemon off;'
