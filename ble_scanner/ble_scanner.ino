/*
 * BLE Scanner - watches for "Logitech K380" and reports connect/disconnect events
 * Flash to the second ESP32 on COM6
 * Serial output: 115200 baud
 */

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define TARGET_NAME "Logitech K380"
#define SCAN_INTERVAL_SEC 2

BLEScan* pBLEScan;

bool lastSeen = false;
unsigned long lastSeenTime = 0;
unsigned long missedScans = 0;

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (dev.getName() == TARGET_NAME) {
      int rssi = dev.getRSSI();
      Serial.printf("[VISIBLE] %s  RSSI:%d dBm\n", TARGET_NAME, rssi);
      lastSeen = true;
      lastSeenTime = millis();
      missedScans = 0;
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("BLE Scanner ready — watching for: " TARGET_NAME);
  BLEDevice::init("ESP32-Scanner");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void loop() {
  bool foundThisScan = false;

  // Run a short scan
  BLEScanResults results = pBLEScan->start(SCAN_INTERVAL_SEC, false);

  for (int i = 0; i < results.getCount(); i++) {
    BLEAdvertisedDevice dev = results.getDevice(i);
    if (dev.getName() == TARGET_NAME) {
      foundThisScan = true;
    }
  }
  pBLEScan->clearResults();

  if (!foundThisScan) {
    missedScans++;
    if (lastSeen) {
      // Just disappeared — likely connected to phone
      Serial.printf("[GONE]    %s not in scan (probably connected elsewhere)  missed:%lu\n",
                    TARGET_NAME, missedScans);
    } else {
      Serial.printf("[ABSENT]  %s not seen  missed:%lu\n", TARGET_NAME, missedScans);
    }
  }

  // If it was gone and just reappeared, that means it disconnected from the phone
  if (foundThisScan && missedScans > 0) {
    unsigned long goneMs = millis() - lastSeenTime;
    Serial.printf("[REAPPEARED] %s back after %lu ms — likely DISCONNECTED from phone\n",
                  TARGET_NAME, goneMs);
  }
}
