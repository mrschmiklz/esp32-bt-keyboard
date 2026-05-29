/*
 * ESP32 BLE Keyboard Emulator
 * Requires: ESP32-BLE-Keyboard library by T-vK
 *   Install: Arduino IDE > Library Manager > search "ESP32 BLE Keyboard"
 *   Or: https://github.com/T-vK/ESP32-BLE-Keyboard
 *
 * Serial protocol (115200 baud, newline-terminated):
 *   TYPE <text>       — type a string
 *   TYPEN <text>      — type a string + press Enter
 *   KEY <name>        — press a special key (ENTER, TAB, ESC, DELETE, UP, DOWN,
 *                       LEFT, RIGHT, HOME, END, PAGEUP, PAGEDOWN, F1..F12, CAPS)
 *   MOD <combo>       — modifier combo (e.g. CTRL+C, WIN+R, CTRL+SHIFT+T)
 *   STATUS            — returns CONNECTED or DISCONNECTED
 */

#define USE_NIMBLE
#include <BleKeyboard.h>

BleKeyboard bleKeyboard("Logitech K380", "Logitech", 100);

bool wasConnected = false;
unsigned long connectedAt = 0;

void setup() {
  Serial.begin(115200);
  bleKeyboard.begin();
  Serial.println("READY");
}

void loop() {
  bool connected = bleKeyboard.isConnected();

  if (!wasConnected && connected) {
    connectedAt = millis();
    Serial.println("EVENT:CONNECTED");
  }
  if (wasConnected && !connected) {
    Serial.printf("EVENT:DISCONNECTED after %lums\n", millis() - connectedAt);
    bleKeyboard.begin();
  }
  wasConnected = connected;

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      handleCommand(line);
    }
  }
  delay(100);
}

void handleCommand(String cmd) {
  if (cmd == "STATUS") {
    Serial.println(bleKeyboard.isConnected() ? "CONNECTED" : "DISCONNECTED");
    return;
  }

  if (!bleKeyboard.isConnected()) {
    Serial.println("ERR:NOT_CONNECTED");
    return;
  }

  if (cmd.startsWith("TYPE ")) {
    bleKeyboard.print(cmd.substring(5));
    Serial.println("OK");
  } else if (cmd.startsWith("TYPEN ")) {
    bleKeyboard.println(cmd.substring(6));
    Serial.println("OK");
  } else if (cmd.startsWith("KEY ")) {
    uint8_t k = resolveSpecialKey(cmd.substring(4));
    if (k) {
      bleKeyboard.write(k);
      Serial.println("OK");
    } else {
      Serial.println("ERR:UNKNOWN_KEY");
    }
  } else if (cmd.startsWith("MOD ")) {
    sendModCombo(cmd.substring(4));
  } else {
    Serial.println("ERR:UNKNOWN_CMD");
  }
}

uint8_t resolveSpecialKey(String key) {
  key.trim();
  if (key == "ENTER" || key == "RETURN") return KEY_RETURN;
  if (key == "ESC")       return KEY_ESC;
  if (key == "BACKSPACE") return KEY_BACKSPACE;
  if (key == "TAB")       return KEY_TAB;
  if (key == "DELETE")    return KEY_DELETE;
  if (key == "INSERT")    return KEY_INSERT;
  if (key == "UP")        return KEY_UP_ARROW;
  if (key == "DOWN")      return KEY_DOWN_ARROW;
  if (key == "LEFT")      return KEY_LEFT_ARROW;
  if (key == "RIGHT")     return KEY_RIGHT_ARROW;
  if (key == "HOME")      return KEY_HOME;
  if (key == "END")       return KEY_END;
  if (key == "PAGEUP")    return KEY_PAGE_UP;
  if (key == "PAGEDOWN")  return KEY_PAGE_DOWN;
  if (key == "CAPS")      return KEY_CAPS_LOCK;
  if (key == "F1")  return KEY_F1;
  if (key == "F2")  return KEY_F2;
  if (key == "F3")  return KEY_F3;
  if (key == "F4")  return KEY_F4;
  if (key == "F5")  return KEY_F5;
  if (key == "F6")  return KEY_F6;
  if (key == "F7")  return KEY_F7;
  if (key == "F8")  return KEY_F8;
  if (key == "F9")  return KEY_F9;
  if (key == "F10") return KEY_F10;
  if (key == "F11") return KEY_F11;
  if (key == "F12") return KEY_F12;
  return 0;
}

void sendModCombo(String combo) {
  // Parses "CTRL+C", "WIN+R", "CTRL+SHIFT+T", "ALT+F4", etc.
  bool hasCtrl  = combo.indexOf("CTRL")  >= 0;
  bool hasShift = combo.indexOf("SHIFT") >= 0;
  bool hasAlt   = combo.indexOf("ALT")   >= 0;
  bool hasWin   = combo.indexOf("WIN")   >= 0 || combo.indexOf("GUI") >= 0;

  // The actual key is the last token after the final '+'
  int lastPlus = combo.lastIndexOf('+');
  String keyStr = (lastPlus >= 0) ? combo.substring(lastPlus + 1) : combo;
  keyStr.trim();

  // Remove known modifier names if the combo has no '+' (e.g. "CTRL" alone)
  if (keyStr == "CTRL" || keyStr == "SHIFT" || keyStr == "ALT" ||
      keyStr == "WIN"  || keyStr == "GUI") {
    Serial.println("ERR:NO_KEY_IN_COMBO");
    return;
  }

  if (hasCtrl)  bleKeyboard.press(KEY_LEFT_CTRL);
  if (hasShift) bleKeyboard.press(KEY_LEFT_SHIFT);
  if (hasAlt)   bleKeyboard.press(KEY_LEFT_ALT);
  if (hasWin)   bleKeyboard.press(KEY_LEFT_GUI);

  uint8_t specialKey = resolveSpecialKey(keyStr);
  if (specialKey) {
    bleKeyboard.press(specialKey);
  } else if (keyStr.length() == 1) {
    bleKeyboard.press((uint8_t)keyStr[0]);
  } else {
    bleKeyboard.releaseAll();
    Serial.println("ERR:UNKNOWN_KEY_IN_COMBO");
    return;
  }

  delay(50);
  bleKeyboard.releaseAll();
  Serial.println("OK");
}
