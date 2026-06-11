/*
 * Ceiling LED Cluster VLC Transmitter (Tx)
 * ESP32-WROVER firmware for Visible Light Communication
 *
 * Controls 4 independent ceiling LEDs with unique data frames
 * Modulation: OOK with Manchester encoding
 * Frame: 6-bit preamble (011100) + 8-bit Manchester data = 14 bits
 * OTA: HTTPS + signature verification via GitHub Releases
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <ArduinoJson.h>

// LED pin definitions
#define LED1 27
#define LED2 14
#define LED3 33
#define LED4 32

// Firmware version
String version = "1.0";

// GitHub OTA
const String GH_OWNER = "QuocWhite";
const String GH_REPO  = "OTA";
const String GH_BRANCH = "main";

// RSA-2048 public key (DER) for firmware signature verification
// Generated with: openssl rsa -in private.key -pubout -outform DER | xxd -i
// Re-generate with: python sign_update.py --embed
#include "src/public_key.h"

// WiFi configuration
WiFiMulti wifi_multi;
const uint32_t connect_timeout_ms = 500;

// Timer
hw_timer_t *my_timer = nullptr;
uint32_t update_counter = 0;

// Frame length: 6-bit preamble (011100) + 8-bit Manchester data = 14 bits
const int FRAME_LEN = 14;

// Manchester encoding: 0 -> 01, 1 -> 10
// Each LED has a unique 4-bit ID for room/position identification
// Pre-computed frames: preamble (6 bits) + Manchester-encoded ID (8 bits)

// LED1: Room ID = 0001 -> Manchester: 01 01 01 10 -> 01010110
int code1[FRAME_LEN] = {0,1,1,1,0,0, 0,1,0,1,0,1,1,0};

// LED2: Room ID = 0011 -> Manchester: 01 01 10 10 -> 01011010
int code2[FRAME_LEN] = {0,1,1,1,0,0, 0,1,0,1,1,0,1,0};

// LED3: Room ID = 0110 -> Manchester: 01 10 10 01 -> 01101001
int code3[FRAME_LEN] = {0,1,1,1,0,0, 0,1,1,0,1,0,0,1};

// LED4: Room ID = 1100 -> Manchester: 10 10 01 01 -> 10100101
int code4[FRAME_LEN] = {0,1,1,1,0,0, 1,0,1,0,0,1,0,1};

// Single shared index for all LEDs (all frames are same length)
int i = 0;

// Mode: VLC (timer) vs Manual (LEDC PWM at 20kHz)
bool vlc_active = true;
const int led_pins[4] = {LED1, LED2, LED3, LED4};
uint8_t ledc_chan[4];
int last_duty[4] = {100, 100, 100, 100};  // 0-100 per LED

// Timer interrupt service routine
void IRAM_ATTR onTimer() {
    i++;
    if (i >= FRAME_LEN) {
        i = 0;
    }
    digitalWrite(LED1, code1[i]);
    digitalWrite(LED2, code2[i]);
    digitalWrite(LED3, code3[i]);
    digitalWrite(LED4, code4[i]);
}

// OTA update
bool checkOtaUpdate();
bool verifySignature(const uint8_t* data, size_t data_len,
                     const uint8_t* sig, size_t sig_len);

void setup() {
    Serial.begin(9600);
    delay(10);

    WiFi.mode(WIFI_STA);

    // Add known WiFi networks
    wifi_multi.addAP("Cong Thanh", "08050616");
    wifi_multi.addAP("Quoc", "12345678h");

    // Scan and list available networks
    int networks_found = WiFi.scanNetworks();
    Serial.println("scan done");
    if (networks_found == 0) {
        Serial.println("no networks found");
    } else {
        Serial.print(networks_found);
        Serial.println(" networks found");
        for (int j = 0; j < networks_found; ++j) {
            Serial.print(j + 1);
            Serial.print(": ");
            Serial.print(WiFi.SSID(j));
            Serial.print(" (");
            Serial.print(WiFi.RSSI(j));
            Serial.print(")");
            Serial.println((WiFi.encryptionType(j) == WIFI_AUTH_OPEN) ? " " : "*");
            delay(10);
        }
    }

    // Connect to the strongest known WiFi
    Serial.println("Connecting Wifi...");
    if (wifi_multi.run() == WL_CONNECTED) {
        Serial.println("");
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
    }

    // Configure LED pins
    pinMode(LED1, OUTPUT);
    pinMode(LED2, OUTPUT);
    pinMode(LED3, OUTPUT);
    pinMode(LED4, OUTPUT);

    // Timer configuration (ESP32 Arduino Core 3.x API):
    //   timerBegin(frequency) -> 1 MHz (1 us per tick)
    //   timerAlarm(timer, ticks, autoreload, reload_count)
    //   Alarm at 250 ticks -> 250 us per bit = 4000 Hz bit rate
    //   To change frequency, adjust the alarm value:
    //     5 kHz -> 200,  7 kHz -> ~143
    my_timer = timerBegin(1000000);       // 1 MHz = 1 us per tick
    timerAttachInterrupt(my_timer, &onTimer);
    timerWrite(my_timer, 0);              // start counter at 0
    timerAlarm(my_timer, 250, true, 0);   // alarm at 250 us, autoreload
    timerStart(my_timer);
}

void setupLedc() {
    for (int n = 0; n < 4; n++) {
        ledcDetach(led_pins[n]);
        ledc_chan[n] = ledcAttach(led_pins[n], 20000, 10);
    }
}

void teardownLedc() {
    for (int n = 0; n < 4; n++) {
        ledcDetach(led_pins[n]);
        pinMode(led_pins[n], OUTPUT);
    }
}

// Simple serial command parser
void handleSerialCommand() {
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "VLC") {
        if (!vlc_active) {
            teardownLedc();
            vlc_active = true;
        }
        timerStart(my_timer);
        Serial.println("OK VLC mode");
        return;
    }

    if (cmd.startsWith("DIM ")) {
        int pct = cmd.substring(4).toInt();
        pct = constrain(pct, 0, 100);

        if (vlc_active) {
            int alarm_val = map(pct, 0, 100, 500, 50);
            timerAlarm(my_timer, alarm_val, true, 0);
            Serial.printf("OK DIM %d (alarm=%d)\n", pct, alarm_val);
        } else {
            for (int n = 0; n < 4; n++) {
                last_duty[n] = pct;
                ledcWrite(ledc_chan[n], map(pct, 0, 100, 0, 1023));
            }
            Serial.printf("OK DIM %d\n", pct);
        }
        return;
    }

    if (cmd.startsWith("FREQ ")) {
        int hz = cmd.substring(5).toInt();
        hz = constrain(hz, 100, 10000);
        if (vlc_active) {
            int alarm_val = 1000000 / hz;
            timerAlarm(my_timer, alarm_val, true, 0);
        }
        Serial.printf("OK FREQ %d\n", hz);
        return;
    }

    for (int n = 1; n <= 4; n++) {
        String prefix = "LED" + String(n) + " ";
        if (cmd.startsWith(prefix)) {
            int idx = n - 1;
            String state = cmd.substring(prefix.length());
            bool on = (state == "ON");
            if (on || state == "OFF") {
                if (vlc_active) {
                    timerStop(my_timer);
                    vlc_active = false;
                    setupLedc();
                }
                last_duty[idx] = on ? 100 : 0;
                ledcWrite(ledc_chan[idx], on ? 1023 : 0);
                Serial.printf("OK LED%d %s\n", n, on ? "ON" : "OFF");
                return;
            }
        }
    }

    Serial.print("? unknown: ");
    Serial.println(cmd);
}

void loop() {
    handleSerialCommand();

    // Maintain WiFi connection
    if (wifi_multi.run(connect_timeout_ms) == WL_CONNECTED) {
        Serial.print("WiFi connected: ");
        Serial.print(WiFi.SSID());
        Serial.print(" ");
        Serial.println(WiFi.RSSI());
    }

    Serial.print("Ver: ");
    Serial.println(version);
    delay(500);

    // Check for OTA updates every ~5 seconds
    if (WiFi.status() == WL_CONNECTED) {
        update_counter++;
        if (update_counter > 10) {
            update_counter = 0;
            Serial.println("Check update");
            if (checkOtaUpdate()) {
                Serial.println("Update downloaded and verified, rebooting...");
                ESP.restart();
            }
        }
    }
}

// Fetch version manifest from GitHub
bool checkOtaUpdate() {
    HTTPClient http;
    String manifest_url = "https://raw.githubusercontent.com/"
                          + GH_OWNER + "/" + GH_REPO + "/" + GH_BRANCH
                          + "/dist/v" + version + "/version.json";

    http.begin(manifest_url);
    int code = http.GET();

    if (code != 200) {
        Serial.println("No update manifest found (code " + String(code) + ")");
        http.end();
        return false;
    }

    // Parse manifest JSON
    String json_str = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str);
    if (err) {
        Serial.println("Failed to parse manifest: " + String(err.c_str()));
        return false;
    }

    String remote_version = doc["version"].as<String>();
    String sha256_hex = doc["sha256"].as<String>();
    String signature_hex = doc["signature"].as<String>();

    Serial.println("Remote version: " + remote_version);

    // Compare versions (simple string compare; use semver lib for production)
    if (remote_version <= version) {
        Serial.println("Already up to date");
        return false;
    }

    Serial.println("New version available, downloading...");

    // Download firmware binary
    String bin_url = "https://raw.githubusercontent.com/"
                     + GH_OWNER + "/" + GH_REPO + "/" + GH_BRANCH
                     + "/dist/v" + remote_version + "/firmware.bin";

    http.begin(bin_url);
    int bin_code = http.GET();
    if (bin_code != 200) {
        Serial.println("Download failed: " + String(bin_code));
        http.end();
        return false;
    }

    // Read binary into buffer
    int bin_len = http.getSize();
    if (bin_len <= 0) {
        Serial.println("Invalid binary size");
        http.end();
        return false;
    }

    uint8_t* bin_data = (uint8_t*)malloc(bin_len);
    if (!bin_data) {
        Serial.println("Out of memory");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    int bytes_read = 0;
    while (bytes_read < bin_len) {
        int r = stream->readBytes(bin_data + bytes_read, bin_len - bytes_read);
        if (r <= 0) break;
        bytes_read += r;
    }

    http.end();

    if (bytes_read != bin_len) {
        Serial.println("Incomplete download");
        free(bin_data);
        return false;
    }

    // Verify SHA256
    uint8_t hash[32];
    mbedtls_sha256(bin_data, bin_len, hash, 0);

    char hash_str[65];
    for (int j = 0; j < 32; j++) {
        sprintf(hash_str + j * 2, "%02x", hash[j]);
    }
    hash_str[64] = '\0';

    if (sha256_hex != String(hash_str)) {
        Serial.println("SHA256 mismatch! Corrupt download.");
        free(bin_data);
        return false;
    }
    Serial.println("SHA256 verified");

    // Verify RSA signature
    size_t sig_len = signature_hex.length() / 2;
    uint8_t* sig = (uint8_t*)malloc(sig_len);
    if (!sig) {
        free(bin_data);
        return false;
    }
    for (size_t j = 0; j < sig_len; j++) {
        char byte_str[3] = {signature_hex[j*2], signature_hex[j*2+1], '\0'};
        sig[j] = strtol(byte_str, nullptr, 16);
    }

    bool valid = verifySignature(bin_data, bin_len, sig, sig_len);
    free(sig);

    if (!valid) {
        Serial.println("Signature verification FAILED! Update rejected.");
        free(bin_data);
        return false;
    }
    Serial.println("Signature verified");

    // Apply update
    if (!Update.begin(bin_len)) {
        Serial.println("Not enough space for update");
        free(bin_data);
        return false;
    }

    Update.write(bin_data, bin_len);
    if (Update.end()) {
        Serial.println("Update applied, rebooting...");
        version = remote_version;
        free(bin_data);
        return true;
    } else {
        Serial.println("Update failed: " + String(Update.errorString()));
        free(bin_data);
        return false;
    }
}

// Verify RSA-2048 PKCS1.5 SHA256 signature
bool verifySignature(const uint8_t* data, size_t data_len,
                     const uint8_t* sig, size_t sig_len) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_parse_public_key(&pk, PUBLIC_KEY, PUBLIC_KEY_LEN);
    if (ret != 0) {
        Serial.println("Failed to parse public key");
        mbedtls_pk_free(&pk);
        return false;
    }

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256,
                            data, data_len, sig, sig_len);
    mbedtls_pk_free(&pk);

    return ret == 0;
}
