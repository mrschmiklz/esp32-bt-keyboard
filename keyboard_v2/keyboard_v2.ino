/*
 * Custom BLE HID Keyboard — NimBLE direct implementation
 * No T-vK wrapper. Uses NimBLEHIDDevice + HIDKeyboardTypes.h directly.
 *
 * Serial protocol (115200 baud, newline-terminated):
 *   TYPE <text>       — type a string
 *   TYPEN <text>      — type a string + Enter
 *   KEY <name>        — special key: ENTER TAB ESC BACKSPACE DELETE SPACE
 *                       UP DOWN LEFT RIGHT HOME END PAGEUP PAGEDOWN
 *                       F1..F12 CAPS INSERT PRINTSCREEN
 *   MOD <combo>       — modifier combo: CTRL+C  WIN+R  CTRL+SHIFT+T
 *   MEDIA <name>      — media key: PLAY PAUSE NEXT PREV STOP MUTE
 *                       VOLUP VOLDOWN
 *   STATUS            — CONNECTED or DISCONNECTED
 *   BONDS             — list bonded device addresses
 *   PAIR              — clear all bonds and re-advertise for fresh pairing
 */

#define US_KEYBOARD
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEHIDDevice.h>
#include <HIDKeyboardTypes.h>
#include <HIDTypes.h>

// NimBLE host call to set a random static address (declared here to avoid
// depending on the internal nimble host include path).
extern "C" int ble_hs_id_set_rnd(const uint8_t *rnd_addr);

// ── Device identity ───────────────────────────────────────────────────────────
#define DEVICE_NAME  "Logitech K380"
#define MANUFACTURER "Logitech"
#define VID          0x046D   // Logitech USB VID
#define PID          0xB342   // K380 BLE PID
#define VER          0x0111

// ── Key timing (ms) ───────────────────────────────────────────────────────────
#define KEY_PRESS_MS   20    // how long a key is held down
#define KEY_RELEASE_MS  5    // gap between consecutive keys in a string

// ── Types ─────────────────────────────────────────────────────────────────────
struct KeyReport {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keys[6];
};

// ── HID Report Descriptor ─────────────────────────────────────────────────────
// Report ID 1: standard 8-byte keyboard (modifier + reserved + 6 keycodes)
// Report ID 2: consumer control / media keys (2 bytes, 10-bit usage)
static const uint8_t hidDescriptor[] = {
    // ── Keyboard (Report ID 1) ──────────────────────────────────────────────
    USAGE_PAGE(1),       0x01,   // Generic Desktop
    USAGE(1),            0x06,   // Keyboard
    COLLECTION(1),       0x01,   // Application
    REPORT_ID(1),        0x01,

    // Modifier byte (8 × 1-bit flags: LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI)
    USAGE_PAGE(1),       0x07,
    USAGE_MINIMUM(1),    0xE0,
    USAGE_MAXIMUM(1),    0xE7,
    LOGICAL_MINIMUM(1),  0x00,
    LOGICAL_MAXIMUM(1),  0x01,
    REPORT_SIZE(1),      0x01,
    REPORT_COUNT(1),     0x08,
    HIDINPUT(1),         0x02,   // Data, Variable, Absolute

    // Reserved byte
    REPORT_COUNT(1),     0x01,
    REPORT_SIZE(1),      0x08,
    HIDINPUT(1),         0x01,   // Constant

    // 6 key-code slots
    REPORT_COUNT(1),     0x06,
    REPORT_SIZE(1),      0x08,
    LOGICAL_MINIMUM(1),  0x00,
    LOGICAL_MAXIMUM(1),  0xFF,   // full usage range
    USAGE_PAGE(1),       0x07,
    USAGE_MINIMUM(1),    0x00,
    USAGE_MAXIMUM(1),    0xFF,
    HIDINPUT(1),         0x00,   // Data, Array

    // LED output (5 bits: NumLock CapsLock ScrollLock Compose Kana)
    REPORT_COUNT(1),     0x05,
    REPORT_SIZE(1),      0x01,
    USAGE_PAGE(1),       0x08,
    USAGE_MINIMUM(1),    0x01,
    USAGE_MAXIMUM(1),    0x05,
    HIDOUTPUT(1),        0x02,   // Data, Variable, Absolute

    // LED padding (3 bits)
    REPORT_COUNT(1),     0x01,
    REPORT_SIZE(1),      0x03,
    HIDOUTPUT(1),        0x01,   // Constant

    END_COLLECTION(0),

    // ── Consumer Control / Media Keys (Report ID 2) ─────────────────────────
    // Raw bytes — macros only support 1-byte args; consumer usages need 16-bit
    0x05, 0x0C,              // Usage Page (Consumer)
    0x09, 0x01,              // Usage (Consumer Control)
    0xA1, 0x01,              // Collection (Application)
    0x85, 0x02,              //   Report ID 2
    0x15, 0x00,              //   Logical Minimum (0)
    0x26, 0xFF, 0x03,        //   Logical Maximum (1023)
    0x19, 0x00,              //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,        //   Usage Maximum (1023)
    0x75, 0x10,              //   Report Size (16 bits)
    0x95, 0x01,              //   Report Count (1)
    0x81, 0x00,              //   Input (Data, Array, Absolute)
    0xC0                     // End Collection
};

// Consumer usage codes (USB HID Usage Tables page 0x0C)
#define MEDIA_NEXT       0x00B5
#define MEDIA_PREV       0x00B6
#define MEDIA_STOP       0x00B7
#define MEDIA_PLAY       0x00CD   // Play/Pause toggle
#define MEDIA_MUTE       0x00E2
#define MEDIA_VOLUP      0x00E9
#define MEDIA_VOLDOWN    0x00EA

// ── Globals ───────────────────────────────────────────────────────────────────
NimBLEHIDDevice*      hid          = nullptr;
NimBLECharacteristic* inputKbd     = nullptr;   // Report ID 1
NimBLECharacteristic* inputMedia   = nullptr;   // Report ID 2
NimBLEServer*         pServer      = nullptr;

bool          connected       = false;
bool          hid_ready       = false;   // true once Windows/host subscribes to CCCD
unsigned long connectedAt     = 0;
uint16_t      connHandle      = 0;        // handle of the active connection
bool          connParamsTuned = false;    // deferred conn-param request done yet?
unsigned long lastAdvKick     = 0;        // advertising-watchdog timestamp

// ── CCCD subscription tracking ───────────────────────────────────────────────
class InputReportCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* pChar, ble_gap_conn_desc* desc, uint16_t subValue) {
        if (subValue > 0) {
            hid_ready = true;
            Serial.println("EVENT:HID_READY");   // host subscribed — safe to send keystrokes
        } else {
            hid_ready = false;
            Serial.println("EVENT:HID_UNSUBSCRIBED");
        }
    }
};

// ── Security / pairing callbacks ──────────────────────────────────────────────
class SecurityCallbacks : public NimBLESecurityCallbacks {
    bool     onConfirmPIN(uint32_t)         { return true; }
    uint32_t onPassKeyRequest()             { return 0; }
    void     onPassKeyNotify(uint32_t pass) { Serial.printf("EVENT:PASSKEY %06d\n", pass); }
    bool     onSecurityRequest()            { return true; }
    void onAuthenticationComplete(ble_gap_conn_desc* desc) {
        if (desc->sec_state.encrypted) {
            Serial.println("EVENT:BONDED");
        } else {
            Serial.println("EVENT:AUTH_FAILED — not encrypted");
            pServer->disconnect(desc->conn_handle);
        }
    }
};

// ── Connection callbacks ──────────────────────────────────────────────────────
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
        connected       = true;
        connectedAt     = millis();
        connHandle      = desc->conn_handle;
        connParamsTuned = false;   // params are tuned later from loop(), once the link settles
        Serial.printf("EVENT:CONNECTED  handle=%d\n", desc->conn_handle);
    }
    void onDisconnect(NimBLEServer* s) override {
        connected  = false;
        hid_ready  = false;
        Serial.printf("EVENT:DISCONNECTED after %lums\n", millis() - connectedAt);
        NimBLEDevice::startAdvertising();
        Serial.println("EVENT:ADVERTISING");
    }
};

// ── BLE init ──────────────────────────────────────────────────────────────────
void startBLE() {
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setDeviceName(DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);   // max TX power (+9 dBm)

    // Use a fixed random *static* BLE address (top two bits of the MSB = 11).
    // This presents a fresh identity to hosts so a stale pairing-key cache tied
    // to the chip's factory address can't break new bonds. Change these bytes
    // if you ever need another clean-slate identity.
    static const uint8_t kRandomAddr[6] = {0x66, 0x55, 0x44, 0x33, 0x22, 0xF1};
    int addrRc = ble_hs_id_set_rnd(kRandomAddr);
    Serial.printf("ble_hs_id_set_rnd rc=%d\n", addrRc);
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

    // Security: bonding with "Just Works" + Secure Connections
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    // Distribute LTK + IRK so the host can reconnect after power cycle
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityCallbacks(new SecurityCallbacks());

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    hid = new NimBLEHIDDevice(pServer);
    hid->manufacturer(MANUFACTURER);
    hid->pnp(0x02, VID, PID, VER);
    hid->hidInfo(0x00, 0x02);   // country=0, flags=Normally Connectable
    hid->reportMap((uint8_t*)hidDescriptor, sizeof(hidDescriptor));
    hid->setBatteryLevel(100);

    inputKbd   = hid->inputReport(1);   // Report ID 1 — keyboard
    inputKbd->setCallbacks(new InputReportCallbacks());
    inputMedia = hid->inputReport(2);   // Report ID 2 — consumer control

    // Boot keyboard protocol (BIOS/UEFI compatibility)
    hid->bootInput();
    hid->bootOutput();

    hid->startServices();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid->hidService()->getUUID());
    adv->addServiceUUID(hid->batteryService()->getUUID());
    adv->addServiceUUID(hid->deviceInfo()->getUUID());
    adv->setScanResponse(true);
    // Fast advertising (20–40ms) so a bonded host re-finds us quickly after a
    // disconnect or a sleep/wake cycle.
    adv->setMinInterval(0x20);   // 32 × 0.625ms = 20ms
    adv->setMaxInterval(0x40);   // 64 × 0.625ms = 40ms
    adv->start();

    Serial.println("READY");
}

// ── Key sending ───────────────────────────────────────────────────────────────
void sendKbdReport(KeyReport* r) {
    inputKbd->setValue((uint8_t*)r, sizeof(KeyReport));
    inputKbd->notify();
    delay(KEY_PRESS_MS);
}

void sendKbdRelease() {
    KeyReport empty = {0, 0, {0,0,0,0,0,0}};
    sendKbdReport(&empty);
}

void pressKey(uint8_t keycode, uint8_t modifier = 0) {
    KeyReport r = {modifier, 0, {keycode,0,0,0,0,0}};
    sendKbdReport(&r);
    sendKbdRelease();
}

void sendMediaKey(uint16_t usage) {
    inputMedia->setValue((uint8_t*)&usage, 2);
    inputMedia->notify();
    delay(KEY_PRESS_MS);
    uint16_t release = 0;
    inputMedia->setValue((uint8_t*)&release, 2);
    inputMedia->notify();
}

void typeString(const String& text) {
    for (int i = 0; i < (int)text.length(); i++) {
        uint8_t c = (uint8_t)text[i];
        if (c >= KEYMAP_SIZE) continue;
        KEYMAP k = keymap[c];
        if (k.usage == 0 && k.modifier == 0) continue;
        uint8_t mod = 0;
        if (k.modifier & KEY_SHIFT) mod |= 0x02;
        if (k.modifier & KEY_CTRL)  mod |= 0x01;
        if (k.modifier & KEY_ALT)   mod |= 0x04;
        pressKey(k.usage, mod);
        delay(KEY_RELEASE_MS);
    }
}

// ── Key name → HID keycode ────────────────────────────────────────────────────
uint8_t resolveKey(String key) {
    key.trim();
    if (key == "ENTER"       || key == "RETURN") return 0x28;
    if (key == "ESC")                             return 0x29;
    if (key == "BACKSPACE")                       return 0x2A;
    if (key == "TAB")                             return 0x2B;
    if (key == "SPACE")                           return 0x2C;
    if (key == "MINUS")                           return 0x2D;
    if (key == "EQUALS")                          return 0x2E;
    if (key == "CAPS")                            return 0x39;
    if (key == "PRINTSCREEN")                     return 0x46;
    if (key == "SCROLLLOCK")                      return 0x47;
    if (key == "PAUSE")                           return 0x48;
    if (key == "INSERT")                          return 0x49;
    if (key == "HOME")                            return 0x4A;
    if (key == "PAGEUP")                          return 0x4B;
    if (key == "DELETE")                          return 0x4C;
    if (key == "END")                             return 0x4D;
    if (key == "PAGEDOWN")                        return 0x4E;
    if (key == "RIGHT")                           return 0x4F;
    if (key == "LEFT")                            return 0x50;
    if (key == "DOWN")                            return 0x51;
    if (key == "UP")                              return 0x52;
    if (key == "NUMLOCK")                         return 0x53;
    if (key == "F1")  return 0x3A; if (key == "F2")  return 0x3B;
    if (key == "F3")  return 0x3C; if (key == "F4")  return 0x3D;
    if (key == "F5")  return 0x3E; if (key == "F6")  return 0x3F;
    if (key == "F7")  return 0x40; if (key == "F8")  return 0x41;
    if (key == "F9")  return 0x42; if (key == "F10") return 0x43;
    if (key == "F11") return 0x44; if (key == "F12") return 0x45;
    return 0;
}

uint16_t resolveMediaKey(String key) {
    key.trim();
    if (key == "NEXT"  )  return MEDIA_NEXT;
    if (key == "PREV"  )  return MEDIA_PREV;
    if (key == "STOP"  )  return MEDIA_STOP;
    if (key == "PLAY"  || key == "PAUSE") return MEDIA_PLAY;
    if (key == "MUTE"  )  return MEDIA_MUTE;
    if (key == "VOLUP" )  return MEDIA_VOLUP;
    if (key == "VOLDOWN") return MEDIA_VOLDOWN;
    return 0;
}

// ── Command handler ───────────────────────────────────────────────────────────
void handleCommand(const String& cmd) {
    // ── Status / management (no connection required) ──────────────────────────
    if (cmd == "STATUS") {
        if (!connected)  Serial.println("DISCONNECTED");
        else if (!hid_ready) Serial.println("CONNECTED_NOT_READY");
        else             Serial.println("READY");
        return;
    }
    if (cmd == "BONDS") {
        int n = NimBLEDevice::getNumBonds();
        Serial.printf("BONDS:%d\n", n);
        for (int i = 0; i < n; i++) {
            NimBLEAddress addr = NimBLEDevice::getBondedAddress(i);
            Serial.println(addr.toString().c_str());
        }
        return;
    }
    if (cmd == "PAIR") {
        if (connected) pServer->disconnect(pServer->getConnectedCount() ? 0 : 0);
        NimBLEDevice::deleteAllBonds();
        NimBLEDevice::startAdvertising();
        Serial.println("OK:PAIRING_MODE");
        return;
    }

    // ── Keystroke commands (require connection) ───────────────────────────────
    if (!connected) {
        Serial.println("ERR:NOT_CONNECTED");
        return;
    }
    if (!hid_ready) {
        Serial.println("ERR:NOT_READY");
        return;
    }

    if (cmd.startsWith("TYPE ")) {
        typeString(cmd.substring(5));
        Serial.println("OK");

    } else if (cmd.startsWith("TYPEN ")) {
        typeString(cmd.substring(6));
        pressKey(0x28);   // Enter
        Serial.println("OK");

    } else if (cmd.startsWith("KEY ")) {
        uint8_t kc = resolveKey(cmd.substring(4));
        if (kc) { pressKey(kc); Serial.println("OK"); }
        else     Serial.println("ERR:UNKNOWN_KEY");

    } else if (cmd.startsWith("MOD ")) {
        String combo = cmd.substring(4);
        uint8_t mod = 0;
        if (combo.indexOf("CTRL")  >= 0) mod |= 0x01;
        if (combo.indexOf("SHIFT") >= 0) mod |= 0x02;
        if (combo.indexOf("ALT")   >= 0) mod |= 0x04;
        if (combo.indexOf("WIN")   >= 0 || combo.indexOf("GUI") >= 0) mod |= 0x08;
        int last   = combo.lastIndexOf('+');
        String key = (last >= 0) ? combo.substring(last + 1) : combo;
        key.trim();
        uint8_t kc = resolveKey(key);
        if (kc == 0 && key.length() == 1) {
            uint8_t c = (uint8_t)key[0];
            if (c < KEYMAP_SIZE) kc = keymap[c].usage;
        }
        if (kc) { pressKey(kc, mod); Serial.println("OK"); }
        else     Serial.println("ERR:UNKNOWN_KEY_IN_COMBO");

    } else if (cmd.startsWith("MEDIA ")) {
        uint16_t usage = resolveMediaKey(cmd.substring(6));
        if (usage) { sendMediaKey(usage); Serial.println("OK"); }
        else        Serial.println("ERR:UNKNOWN_MEDIA_KEY");

    } else {
        Serial.println("ERR:UNKNOWN_CMD");
    }
}

// ── Arduino entry points ──────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    startBLE();
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) handleCommand(line);
    }

    unsigned long now = millis();

    // Deferred connection-parameter tuning: request HID-friendly params once,
    // ~1.5s after connecting, when pairing/discovery has settled. Windows honors
    // a post-settle request far more reliably than one fired at the instant of
    // connect. min 7.5ms · max 30ms · latency 4 · supervision timeout 5000ms.
    if (connected && !connParamsTuned && now - connectedAt > 1500) {
        pServer->updateConnParams(connHandle, 6, 24, 4, 500);
        connParamsTuned = true;
        Serial.println("EVENT:CONNPARAMS_TUNED");
    }

    // Advertising watchdog: whenever we're not connected, make sure we are
    // advertising so a bonded host can always reconnect. Guards against edge
    // cases where an abnormal drop leaves advertising stopped.
    if (!connected && now - lastAdvKick > 3000) {
        lastAdvKick = now;
        if (!NimBLEDevice::getAdvertising()->isAdvertising()) {
            NimBLEDevice::startAdvertising();
            Serial.println("EVENT:ADV_WATCHDOG_RESTART");
        }
    }

    delay(10);
}
