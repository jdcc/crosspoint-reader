#pragma once

#include <string>

// Optional WireGuard tunnel for reaching a catalog server on the home network
// from another network. Configured by /.crosspoint/wireguard.json:
//
//   {"private_key": "...", "address": "10.8.0.2", "netmask": "255.255.255.0",
//    "public_key": "...", "preshared_key": "", "endpoint": "vpn.example.org",
//    "port": 51820, "keepalive": 25}
//
// Copy the file with an SD card reader or USB Drive mode: it holds a private key, so
// the web file manager refuses it (see ProtectedPaths.h).
// Without that file, or in builds without CROSSPOINT_WIREGUARD, every call is a no-op.
namespace wgtunnel {

// Call with WiFi connected, before the first request to `url`. Leaves routing
// alone when the server answers directly; otherwise brings the tunnel up and
// routes all traffic through it. False when a tunnel was needed and failed.
#if CROSSPOINT_WIREGUARD
bool ensureRoute(const std::string& url);
// Tears the tunnel down and restores direct routing.
void down();
#else
inline bool ensureRoute(const std::string&) { return true; }
inline void down() {}
#endif

}  // namespace wgtunnel
