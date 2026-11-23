/*
 * ESP32-S3 USB HID keyboard — controller on UART, target on native USB
 *
 * For dual USB-C boards:
 *   - UART / USB-serial port  → controller PC (this Serial @ 115200)
 *   - Native USB OTG          → controlled PC (USB HID keyboard)
 *
 * Board settings (Arduino IDE / arduino-cli):
 *   Board: ESP32-S3 ...
 *   USB CDC On Boot: Disabled   (keeps Serial on UART, not the USB-CDC port)
 *   USB Mode: USB-OTG enabled (HID device)
 *
 * Same serial protocol as keyboard_v2 (TYPE, KEY, MOD, STATUS, WHOAMI, ...).
 * BLE commands BONDS / PAIR return ERR:NOT_BLE (USB mode has no pairing).
 *
 * FQBN example:
 *   arduino-cli compile --fqbn esp32:esp32:esp32s3 .../keyboard_usb_s3
 */

#include <Arduino.h>
#ifndef ARDUINO_USB_MODE
#error Select a board with native USB (ESP32-S3). See docs/DUAL_HOST.md
#elif ARDUINO_USB_MODE == 1
#warning Use USB mode: USB-OTG (TinyUSB), CDC On Boot: Disabled for UART control link
void setup() {}
void loop() {}
#else

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "tusb.h"

// ── Timing ───────────────────────────────────────────────────────────────────
#define KEY_PRESS_MS   20
#define KEY_RELEASE_MS  5

USBHIDKeyboard UsbKbd;

static bool hostReady() {
    return (bool)USB && tud_mounted();
}

// ── Key name → USB HID keycode (subset matching keyboard_v2) ─────────────────
uint8_t resolveKey(String key) {
    key.trim();
    key.toUpperCase();
    if (key == "ENTER" || key == "RETURN") return KEY_RETURN;
    if (key == "ESC") return KEY_ESC;
    if (key == "BACKSPACE") return KEY_BACKSPACE;
    if (key == "TAB") return KEY_TAB;
    if (key == "SPACE") return KEY_SPACE;
    if (key == "DELETE") return KEY_DELETE;
    if (key == "HOME") return KEY_HOME;
    if (key == "END") return KEY_END;
    if (key == "PAGEUP") return KEY_PAGE_UP;
    if (key == "PAGEDOWN") return KEY_PAGE_DOWN;
    if (key == "RIGHT") return KEY_RIGHT_ARROW;
    if (key == "LEFT") return KEY_LEFT_ARROW;
    if (key == "DOWN") return KEY_DOWN_ARROW;
    if (key == "UP") return KEY_UP_ARROW;
    if (key == "F1") return KEY_F1; if (key == "F2") return KEY_F2;
    if (key == "F3") return KEY_F3; if (key == "F4") return KEY_F4;
    if (key == "F5") return KEY_F5; if (key == "F6") return KEY_F6;
    if (key == "F7") return KEY_F7; if (key == "F8") return KEY_F8;
    if (key == "F9") return KEY_F9; if (key == "F10") return KEY_F10;
    if (key == "F11") return KEY_F11; if (key == "F12") return KEY_F12;
    return 0;
}

uint8_t modifierFromCombo(const String& combo) {
    uint8_t mod = 0;
    String u = combo;
    u.toUpperCase();
    if (u.indexOf("CTRL") >= 0) mod |= KEY_LEFT_CTRL;
    if (u.indexOf("SHIFT") >= 0) mod |= KEY_LEFT_SHIFT;
    if (u.indexOf("ALT") >= 0) mod |= KEY_LEFT_ALT;
    if (u.indexOf("WIN") >= 0 || u.indexOf("GUI") >= 0) mod |= KEY_LEFT_GUI;
    return mod;
}

void tapKey(uint8_t keycode, uint8_t modifiers = 0) {
    if (modifiers) UsbKbd.press(modifiers);
    UsbKbd.press(keycode);
    delay(KEY_PRESS_MS);
    UsbKbd.release(keycode);
    if (modifiers) UsbKbd.release(modifiers);
    delay(KEY_RELEASE_MS);
}

void typeString(const String& text) {
    for (unsigned i = 0; i < text.length(); i++) {
        UsbKbd.write((uint8_t)text[i]);
        delay(KEY_RELEASE_MS);
    }
}

void handleCommand(const String& cmd) {
    if (cmd == "WHOAMI") {
        Serial.println("LINK:USB");
        return;
    }
    if (cmd == "STATUS") {
        if (!((bool)USB)) Serial.println("DISCONNECTED");
        else if (!tud_mounted()) Serial.println("CONNECTED_NOT_READY");
        else Serial.println("READY");
        return;
    }
    if (cmd == "BONDS" || cmd == "PAIR") {
        Serial.println("ERR:NOT_BLE");
        return;
    }

    if (!hostReady()) {
        Serial.println(hostReady() ? "ERR:NOT_READY" : "ERR:NOT_CONNECTED");
        return;
    }

    if (cmd.startsWith("TYPE ")) {
        typeString(cmd.substring(5));
        Serial.println("OK");
    } else if (cmd.startsWith("TYPEN ")) {
        typeString(cmd.substring(6));
        tapKey(KEY_RETURN);
        Serial.println("OK");
    } else if (cmd.startsWith("KEY ")) {
        uint8_t kc = resolveKey(cmd.substring(4));
        if (kc) { tapKey(kc); Serial.println("OK"); }
        else Serial.println("ERR:UNKNOWN_KEY");
    } else if (cmd.startsWith("MOD ")) {
        String combo = cmd.substring(4);
        uint8_t mod = modifierFromCombo(combo);
        int last = combo.lastIndexOf('+');
        String key = (last >= 0) ? combo.substring(last + 1) : combo;
        key.trim();
        uint8_t kc = resolveKey(key);
        if (kc == 0 && key.length() == 1) {
            UsbKbd.press(mod);
            UsbKbd.write((uint8_t)key[0]);
            delay(KEY_PRESS_MS);
            UsbKbd.releaseAll();
            Serial.println("OK");
        } else if (kc) {
            tapKey(kc, mod);
            Serial.println("OK");
        } else Serial.println("ERR:UNKNOWN_KEY_IN_COMBO");
    } else if (cmd.startsWith("MEDIA ")) {
        // Consumer media keys are not wired in this USB build yet.
        Serial.println("ERR:MEDIA_NOT_USB_YET");
    } else {
        Serial.println("ERR:UNKNOWN_CMD");
    }
}

void setup() {
    Serial.begin(115200);
    UsbKbd.begin();
    USB.begin();
    delay(300);
    Serial.println("READY");
    Serial.println("EVENT:USB_HID_STACK");
    Serial.println("LINK:USB");
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) handleCommand(line);
    }
    delay(2);
}

#endif /* ARDUINO_USB_MODE */