#include "WireGuardTunnel.h"

#if CROSSPOINT_WIREGUARD

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_wireguard.h>
#include <lwip/tcpip.h>

namespace {

constexpr const char* CONFIG_PATH = "/.crosspoint/wireguard.json";
constexpr uint32_t DIRECT_PROBE_MS = 2000;
constexpr uint32_t ENDPOINT_DNS_MS = 5000;
constexpr uint32_t CLOCK_SYNC_MS = 6000;
constexpr uint32_t HANDSHAKE_MS = 10000;

// The library keeps pointers into these strings while the tunnel is up.
struct Settings {
  std::string privateKey, publicKey, presharedKey, address, netmask, endpoint;
  uint16_t port = 51820;
  uint16_t keepalive = 25;
};

Settings settings;
wireguard_config_t config = ESP_WIREGUARD_CONFIG_DEFAULT();
wireguard_ctx_t ctx = {};
bool tunnelUp = false;

// The library calls lwIP (netif, udp, dns, timers) directly, so every call into
// it must hold the TCP/IP core lock; the core asserts otherwise. The lock is held
// only for each call (key setup briefly stalls the TCP/IP thread), never across
// the waits between them.
template <typename Fn>
esp_err_t locked(Fn&& fn) {
  LOCK_TCPIP_CORE();
  const esp_err_t err = fn();
  UNLOCK_TCPIP_CORE();
  return err;
}

bool loadSettings() {
  if (!Storage.exists(CONFIG_PATH)) return false;
  HalFile file;
  if (!Storage.openFileForRead("WG", CONFIG_PATH, file)) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    LOG_ERR("WG", "Config parse error: %s", err.c_str());
    return false;
  }
  settings.privateKey = doc["private_key"] | "";
  settings.publicKey = doc["public_key"] | "";
  settings.presharedKey = doc["preshared_key"] | "";
  settings.address = doc["address"] | "";
  settings.netmask = doc["netmask"] | "255.255.255.255";
  settings.endpoint = doc["endpoint"] | "";
  settings.port = doc["port"] | 51820;
  settings.keepalive = doc["keepalive"] | 25;
  if (settings.privateKey.empty() || settings.publicKey.empty() || settings.address.empty() ||
      settings.endpoint.empty()) {
    LOG_ERR("WG", "Config is missing private_key, public_key, address or endpoint");
    return false;
  }
  return true;
}

// Splits scheme://host[:port]/... ; the port defaults from the scheme.
bool hostAndPort(const std::string& url, std::string& host, uint16_t& port) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return false;
  port = url.compare(0, schemeEnd, "https") == 0 ? 443 : 80;
  const size_t hostStart = schemeEnd + 3;
  size_t hostEnd = url.find_first_of("/?#", hostStart);
  if (hostEnd == std::string::npos) hostEnd = url.size();
  host = url.substr(hostStart, hostEnd - hostStart);
  const size_t at = host.rfind('@');
  if (at != std::string::npos) host.erase(0, at + 1);
  const size_t colon = host.rfind(':');
  if (colon != std::string::npos && host.find(']') == std::string::npos) {
    port = static_cast<uint16_t>(atoi(host.c_str() + colon + 1));
    host.erase(colon);
  }
  return !host.empty() && port != 0;
}

bool answersDirectly(const std::string& host, const uint16_t port) {
  WiFiClient client;
  const bool ok = client.connect(host.c_str(), port, DIRECT_PROBE_MS);
  client.stop();
  return ok;
}

}  // namespace

namespace wgtunnel {

bool ensureRoute(const std::string& url) {
  if (tunnelUp) return true;
  if (!loadSettings()) return true;  // no tunnel configured: nothing to do

  std::string host;
  uint16_t port = 0;
  if (hostAndPort(url, host, port) && answersDirectly(host, port)) {
    LOG_INF("WG", "%s answers directly; tunnel not needed", host.c_str());
    return true;
  }

  // The peer drops a handshake whose timestamp is not newer than the last one it
  // accepted. After a cold boot the clock resumes from a saved floor that can lag,
  // so wait for the sync Wi-Fi started at connect (startSync() leaves a running
  // one alone; syncNow() would restart it). Already synced: no wait at all.
  if (!trustedtime::syncedThisBoot()) {
    trustedtime::startSync();
    for (const uint32_t start = millis(); !trustedtime::syncedThisBoot() && millis() - start < CLOCK_SYNC_MS;) {
      delay(100);
    }
    if (!trustedtime::syncedThisBoot()) LOG_INF("WG", "Clock not synced yet; handshaking with the saved time");
  }

  config.private_key = settings.privateKey.c_str();
  config.public_key = settings.publicKey.c_str();
  config.preshared_key = settings.presharedKey.empty() ? nullptr : settings.presharedKey.c_str();
  config.address = settings.address.c_str();
  config.netmask = settings.netmask.c_str();
  config.endpoint = settings.endpoint.c_str();
  config.port = settings.port;
  config.persistent_keepalive = settings.keepalive;

  if (locked([] { return esp_wireguard_init(&config, &ctx); }) != ESP_OK) {
    LOG_ERR("WG", "Init failed");
    return false;
  }
  // connect() answers ESP_ERR_RETRY while the endpoint hostname is resolving.
  esp_err_t err = ESP_ERR_RETRY;
  for (const uint32_t start = millis(); err == ESP_ERR_RETRY && millis() - start < ENDPOINT_DNS_MS; delay(100)) {
    err = locked([] { return esp_wireguard_connect(&ctx); });
  }
  if (err != ESP_OK) {
    LOG_ERR("WG", "Connect to %s failed: %d", settings.endpoint.c_str(), static_cast<int>(err));
    // A config the library rejects leaves no interface, and disconnect asserts on that.
    if (ctx.netif) locked([] { return esp_wireguard_disconnect(&ctx); });
    return false;
  }
  const uint32_t start = millis();
  while (locked([] { return esp_wireguard_peer_is_up(&ctx); }) != ESP_OK) {
    if (millis() - start >= HANDSHAKE_MS) {
      LOG_ERR("WG", "No handshake from %s:%u", settings.endpoint.c_str(), settings.port);
      locked([] { return esp_wireguard_disconnect(&ctx); });
      return false;
    }
    delay(100);
  }
  // The peer accepts only the tunnel subnet by default; send everything through it.
  // Without this route, traffic for the LAN reaches the tunnel and is dropped.
  if (locked([] { return esp_wireguard_add_allowed_ip(&ctx, "0.0.0.0", "0.0.0.0"); }) != ESP_OK) {
    LOG_ERR("WG", "Could not route traffic through the tunnel");
    locked([] { return esp_wireguard_disconnect(&ctx); });
    return false;
  }
  locked([] { return esp_wireguard_set_default(&ctx); });
  tunnelUp = true;
  LOG_INF("WG", "Tunnel up after %lums", millis() - start);
  return true;
}

void down() {
  if (!tunnelUp) return;
  locked([] {
    esp_wireguard_restore_default(&ctx);
    return esp_wireguard_disconnect(&ctx);
  });
  tunnelUp = false;
}

}  // namespace wgtunnel

#endif  // CROSSPOINT_WIREGUARD
