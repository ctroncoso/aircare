/* Deployment-specific overrides for the AirCare web UI.
 * In Docker this file is generated from environment variables by an entrypoint
 * (see docker-compose.yml + entrypoint.sh) so secrets stay out of the image.
 * Values below are dev defaults; they are overridden at runtime. */
window.AIRCARE_CONFIG = {
  broker: "4532efaca6c5432ea63dbae9a7690ef4.s1.eu.hivemq.cloud",
  wsPort: 8884,
  user: "aircaredevice",
  pass: "aircarepassword",
  grafanaBase: "http://localhost:3001",
  grafanaDashboardUid: "aircare-main",
  configJsonUrl: "config.json"
};
