/*
 * BLE HID Receiver — connects to "Logitech K380" and prints received keystrokes
 * Flash to second ESP32 on COM6
 * Serial: 115200 baud
 */

#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>

#define TARGET_NAME "Logitech K380"

static BLEUUID hidServiceUUID((uint16_t)0x1812);
static BLEUUID reportCharUUID((uint16_t)0x2A4D);

BLEClient*    pClient     = nullptr;
BLEAddress*   pServerAddr = nullptr;
bool          doConnect   = false;
bool          connected   = false;

// Basic US HID keycode -> char (unshifted)
static const char hidUnshifted[] = {
  0,    0,    0,    0,    'a',  'b',  'c',  'd',   // 0x00-0x07
  'e',  'f',  'g',  'h',  'i',  'j',  'k',  'l',   // 0x08-0x0F
  'm',  'n',  'o',  'p',  'q',  'r',  's',  't',   // 0x10-0x17
  'u',  'v',  'w',  'x',  'y',  'z',  '1',  '2',   // 0x18-0x1F
  '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',   // 0x20-0x27
  '\n', 0x1B, '\b', '\t', ' ',  '-',  '=',  '[',   // 0x28-0x2F
  ']',  '\\', 0,    ';',  '\'', '`',  ',',  '.',   // 0x30-0x37
  '/'                                                // 0x38
};
static const char hidShifted[] = {
  0,    0,    0,    0,    'A',  'B',  'C',  'D',
  'E',  'F',  'G',  'H',  'I',  'J',  'K',  'L',
  'M',  'N',  'O',  'P',  'Q',  'R',  'S',  'T',
  'U',  'V',  'W',  'X',  'Y',  'Z',  '!',  '@',
  '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',
  '\n', 0x1B, '\b', '\t', ' ',  '_',  '+',  '{',
  '}',  '|',  0,    ':',  '"',  '~',  '<',  '>',
  '?'
};

void decodeReport(uint8_t* data, size_t len) {
  if (len < 8) {
    Serial.printf("  [short report len=%d]\n", len);
    return;
  }

  uint8_t mod  = data[0];
  bool shift   = (mod & 0x22) != 0;
  bool ctrl    = (mod & 0x11) != 0;
  bool alt     = (mod & 0x44) != 0;
  bool gui     = (mod & 0x88) != 0;

  String pressed = "";
  for (int i = 2; i < 8; i++) {
    uint8_t kc = data[i];
    if (kc == 0) continue;

    if (ctrl)  pressed += "CTRL+";
    if (alt)   pressed += "ALT+";
    if (gui)   pressed += "GUI+";

    if (kc < sizeof(hidUnshifted) && hidUnshifted[kc]) {
      char c = shift ? hidShifted[kc] : hidUnshifted[kc];
      if (c == '\n')       pressed += "<ENTER>";
      else if (c == '\b')  pressed += "<BACKSPACE>";
      else if (c == '\t')  pressed += "<TAB>";
      else if (c == 0x1B)  pressed += "<ESC>";
      else                 pressed += c;
    } else {
      // Special keys
      if      (kc == 0x4F) pressed += "<RIGHT>";
      else if (kc == 0x50) pressed += "<LEFT>";
      else if (kc == 0x51) pressed += "<DOWN>";
      else if (kc == 0x52) pressed += "<UP>";
      else if (kc == 0x4A) pressed += "<HOME>";
      else if (kc == 0x4D) pressed += "<END>";
      else if (kc == 0x4B) pressed += "<PAGEUP>";
      else if (kc == 0x4E) pressed += "<PAGEDOWN>";
      else if (kc == 0x4C) pressed += "<DELETE>";
      else if (kc >= 0x3A && kc <= 0x45) { pressed += "<F"; pressed += (kc - 0x39); pressed += ">"; }
      else pressed += "<0x" + String(kc, HEX) + ">";
    }
  }

  if (pressed.length() > 0) {
    Serial.println("KEY: " + pressed);
  }
}

void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* data, size_t len, bool isNotify) {
  decodeReport(data, len);
}

class ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* c) {
    connected = true;
    Serial.println("EVENT:CONNECTED");
  }
  void onDisconnect(BLEClient* c) {
    connected = false;
    Serial.println("EVENT:DISCONNECTED — will rescan");
    doConnect = false;
  }
};

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (dev.getName() == TARGET_NAME && dev.haveServiceUUID() == false || dev.getName() == TARGET_NAME) {
      if (dev.getName() == TARGET_NAME) {
        Serial.printf("Found: %s  RSSI:%d\n", dev.getName().c_str(), dev.getRSSI());
        pServerAddr = new BLEAddress(dev.getAddress());
        doConnect = true;
        BLEDevice::getScan()->stop();
      }
    }
  }
};

bool connectToKeyboard() {
  Serial.println("Connecting...");
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new ClientCallbacks());
  pClient->connect(*pServerAddr);

  BLERemoteService* pHIDService = pClient->getService(hidServiceUUID);
  if (!pHIDService) {
    Serial.println("ERR: HID service not found");
    pClient->disconnect();
    return false;
  }

  // Get all characteristics and subscribe to any report that supports notify
  std::map<std::string, BLERemoteCharacteristic*>* chars = pHIDService->getCharacteristics();
  int subscribed = 0;
  for (auto& c : *chars) {
    BLERemoteCharacteristic* ch = c.second;
    if (ch->getUUID().equals(reportCharUUID) && ch->canNotify()) {
      ch->registerForNotify(notifyCallback);
      Serial.printf("Subscribed to report char handle 0x%04X\n", ch->getHandle());
      subscribed++;
    }
  }

  if (subscribed == 0) {
    Serial.println("ERR: no notifiable report characteristics found");
    pClient->disconnect();
    return false;
  }

  Serial.printf("Ready — subscribed to %d report(s)\n", subscribed);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("BLE HID Receiver — looking for: " TARGET_NAME);
  BLEDevice::init("ESP32-HIDReceiver");

  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  pScan->setActiveScan(true);
  pScan->start(0); // scan indefinitely until found
}

void loop() {
  if (doConnect && !connected) {
    doConnect = false;
    if (!connectToKeyboard()) {
      // retry scan
      delay(2000);
      BLEDevice::getScan()->start(0);
    }
  }

  if (!connected && pClient == nullptr) {
    // nothing — waiting for scan callback
  }

  if (!connected && pClient != nullptr && !doConnect) {
    delay(3000);
    Serial.println("Rescanning...");
    delete pClient;
    pClient = nullptr;
    BLEDevice::getScan()->start(0);
  }

  delay(100);
}
