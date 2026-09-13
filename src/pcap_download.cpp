// PCAP Download Server — soft AP + minimal HTTP file browser.
// AP: SSID "Download_WIFI", WPA2 "P@ssw0rd@123", IP 192.168.4.1
// Routes:
//   GET /          → HTML file listing with download links and delete buttons
//   GET /dl/<name> → stream the .pcap file as octet-stream
//   GET /del/<name>→ delete the file, redirect to /
#include "pcap_download.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "storage.h"
#include <stdio.h>
#include <string.h>

#define AP_SSID "Download_WIFI"
#define AP_PASS "P@ssw0rd@123"

static WebServer *s_server  = nullptr;
static bool       s_active  = false;

// ── Helpers ───────────────────────────────────────────────────────────────────
static float bytes_to_mb(size_t b) { return (float)b / (1024.0f * 1024.0f); }

// Labels for each JSON log file shown in the download page
struct JsonLogLabel { const char *filename; const char *icon; const char *color; const char *title; };
static const JsonLogLabel JSON_LABELS[] = {
    { "flock_log.json",       "&#x1F4F7;", "#ffd700", "FLOCK CAMERAS"   },
    { "ble_detect_log.json",  "&#x1F6F8;", "#ff6b35", "BLE DETECTIONS"  },
    { "ble_sniff_log.json",   "&#x1F4E1;", "#44bbff", "BLE SNIFF LOG"   },
    { "skyspy_log.json",      "&#x1F6A7;", "#bb88ff", "DRONE LOG"       },
};
static const int JSON_LABEL_COUNT = sizeof(JSON_LABELS) / sizeof(JSON_LABELS[0]);

static void send_index() {
    // PCAP captures
    String files_html = "";
    int file_count = 0;
    File root = storage_fs().open("/");
    if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
            String name = String(f.name());
            if (name.startsWith("pcap_") && name.endsWith(".pcap")) {
                float mb = bytes_to_mb(f.size());
                char mb_buf[16]; snprintf(mb_buf, sizeof(mb_buf), "%.2f MB", mb);
                files_html += "<li><a href='/dl/" + name + "'>" + name + "</a>"
                              "<span class='sz'>" + mb_buf + "</span>"
                              "<a class='del' href='/del/" + name + "'>&#x1F5D1;</a></li>";
                file_count++;
            }
            f = root.openNextFile();
        }
    }
    if (file_count == 0)
        files_html = "<li class='empty'>No capture files yet.</li>";

    // JSON logs — one section per known log file that exists
    String logs_html = "";
    for (int i = 0; i < JSON_LABEL_COUNT; i++) {
        const auto &lbl = JSON_LABELS[i];
        String path = String("/") + lbl.filename;
        if (!storage_fs().exists(path)) continue;
        File fj = storage_fs().open(path, "r");
        float kb = fj ? (float)fj.size() / 1024.0f : 0;
        if (fj) fj.close();
        char kb_buf[16]; snprintf(kb_buf, sizeof(kb_buf), "%.1f KB", kb);
        logs_html += String("<h2 style='color:") + lbl.color +
                     ";letter-spacing:2px;font-size:1em;margin-top:20px'>" +
                     lbl.icon + " " + lbl.title + "</h2><ul>"
                     "<li><a href='/dl/" + lbl.filename + "'>" + lbl.filename + "</a>"
                     "<span class='sz'>" + kb_buf + "</span>"
                     "<a class='del' href='/del/" + lbl.filename + "' title='Clear log'>&#x1F5D1;</a></li></ul>";
    }

    float used_mb  = bytes_to_mb(storage_used_bytes());
    float total_mb = bytes_to_mb(storage_total_bytes());
    char storage[48];
    snprintf(storage, sizeof(storage), "%.2f MB / %.2f MB used", used_mb, total_mb);

    String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width'>
<title>OUI Spy PCAP Downloads</title>
<style>
*{box-sizing:border-box}
body{font-family:monospace;background:#0a0a14;color:#ccc;padding:20px;max-width:600px;margin:0 auto}
h1{color:#4499ff;letter-spacing:2px;font-size:1.3em}
p{color:#555;font-size:.85em}
ul{list-style:none;padding:0}
li{display:flex;align-items:center;gap:8px;padding:10px 12px;border:1px solid #1a1a2e;
   border-radius:6px;margin:6px 0;background:#0e0e1c}
a{color:#44dd88;text-decoration:none}
a:hover{text-decoration:underline}
.sz{color:#555;font-size:.85em;margin-left:auto}
.del{color:#ff4545;font-size:1.1em;margin-left:8px;cursor:pointer}
.empty{color:#333;justify-content:center}
.bar{height:6px;background:#1a1a2e;border-radius:3px;margin:16px 0}
.fill{height:6px;background:#4499ff;border-radius:3px}
footer{margin-top:24px;color:#333;font-size:.8em;text-align:center}
</style></head><body>
<h1>&#x1F4E1; PCAP Downloads</h1>
<p>Connect to <b>Download_WIFI</b> and open <b>192.168.4.1</b></p>
<ul>)";
    html += files_html;
    html += "</ul>";
    html += logs_html;
    html += "<div class='bar'><div class='fill' style='width:";
    float pct = (storage_total_bytes() > 0)
                ? (100.0f * storage_used_bytes() / storage_total_bytes()) : 0;
    html += String((int)pct);
    html += "%'></div></div><p>" + String(storage) + "</p>";
    html += "<form method='get' action='/delall' style='margin-top:12px'>"
            "<button type='submit' style='background:#1a0505;color:#ff4545;border:1px solid #ff4545;"
            "border-radius:6px;padding:10px 20px;font-family:monospace;font-size:.9em;cursor:pointer;width:100%'>"
            "Delete all captures</button></form>";
    html += "<footer>OUI Spy &mdash; oui-spy-waveshare</footer></body></html>";

    s_server->send(200, "text/html", html);
}

static void handle_dl() {
    // URI: /dl/<filename> — serves .pcap and .json; rejects path traversal
    String uri  = s_server->uri();
    String name = uri.substring(4); // strip "/dl/"
    bool allowed = (name.endsWith(".pcap") || name.endsWith(".json")) &&
                   name.indexOf('/') < 0 && name.indexOf("..") < 0 && name.length() > 0;
    if (!allowed) {
        s_server->send(400, "text/plain", "Bad request");
        return;
    }
    String path = "/" + name;
    if (!storage_fs().exists(path)) {
        s_server->send(404, "text/plain", "Not found");
        return;
    }
    File f = storage_fs().open(path, "r");
    if (!f) { s_server->send(500, "text/plain", "Open failed"); return; }
    s_server->sendHeader("Content-Disposition", "attachment; filename=" + name);
    const char *ct = name.endsWith(".json") ? "application/json" : "application/octet-stream";
    s_server->streamFile(f, ct);
    f.close();
}

static void handle_deljson() {
    storage_fs().remove("/flock_log.json");
    s_server->sendHeader("Location", "/");
    s_server->send(302, "text/plain", "");
}

static void handle_del() {
    String uri  = s_server->uri();
    String name = uri.substring(5); // strip "/del/"
    if (name.length() > 0) storage_fs().remove("/" + name);
    s_server->sendHeader("Location", "/");
    s_server->send(302, "text/plain", "");
}

static void handle_delall() {
    // Iterate by known filename pattern — deleting while walking a LittleFS
    // directory iterator corrupts it and skips files.
    char path[24];
    for (int i = 1; i <= 999; i++) {
        snprintf(path, sizeof(path), "/pcap_%03d.pcap", i);
        if (!storage_fs().exists(path)) break;
        storage_fs().remove(path);
    }
    s_server->sendHeader("Location", "/");
    s_server->send(302, "text/plain", "");
}

// ── Public API ────────────────────────────────────────────────────────────────
void pcap_download_start() {
    if (s_active) return;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[pcap_dl] AP started: %s / %s  IP=%s\n",
                  AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());

    s_server = new WebServer(80);
    s_server->on("/",        HTTP_GET, send_index);
    s_server->on("/delall",  HTTP_GET, handle_delall);
    s_server->on("/deljson", HTTP_GET, handle_deljson);
    s_server->onNotFound([]() {
        String uri = s_server->uri();
        if (uri.startsWith("/dl/")) handle_dl();
        else if (uri.startsWith("/del/")) handle_del();
        else s_server->send(404, "text/plain", "Not found");
    });
    s_server->begin();
    s_active = true;
}

void pcap_download_tick() {
    if (s_active && s_server) s_server->handleClient();
}

void pcap_download_stop() {
    if (!s_active) return;
    if (s_server) { s_server->stop(); delete s_server; s_server = nullptr; }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    s_active = false;
    Serial.println("[pcap_dl] AP stopped");
}

bool pcap_download_active() { return s_active; }
