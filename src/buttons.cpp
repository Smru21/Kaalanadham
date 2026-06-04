#include "buttons.h"

void ButtonManager::begin() {
    uint8_t pins[BTN_COUNT] = {
        BTN_PLAY_PIN,
        BTN_NEXT_PIN,
        BTN_MENU_PIN,
        BTN_OK_PIN
    };

    for (int i = 0; i < BTN_COUNT; i++) {
        buttons[i].pin = pins[i];
        pinMode(pins[i], INPUT_PULLUP);
        buttons[i].stableState = HIGH;
        buttons[i].lastRawState = HIGH;
        buttons[i].lastChangeTime = 0;
        buttons[i].pressedFlag = false;
    }
}

void ButtonManager::update() {
    unsigned long now = millis();

    for (int i = 0; i < BTN_COUNT; i++) {
        bool raw = digitalRead(buttons[i].pin);

        // If raw state changed, restart debounce timer
        if (raw != buttons[i].lastRawState) {
            buttons[i].lastChangeTime = now;
            buttons[i].lastRawState = raw;
        }

        // If stable for DEBOUNCE_MS, accept new state
        if ((now - buttons[i].lastChangeTime) >= DEBOUNCE_MS) {
            if (raw != buttons[i].stableState) {
                buttons[i].stableState = raw;

                // Falling edge: HIGH → LOW = button pressed
                if (raw == LOW) {
                    buttons[i].pressedFlag = true;
                }
            }
        }
    }
}

bool ButtonManager::pressed(ButtonID id) {
    if (id >= BTN_COUNT) return false;

    if (buttons[id].pressedFlag) {
        buttons[id].pressedFlag = false;   // Clear flag — only fires once
        return true;
    }
    return false;
}

bool ButtonManager::isHeld(ButtonID id) {
    if (id >= BTN_COUNT) return false;
    return (buttons[id].stableState == LOW);
}

// Add to buttons.cpp:
void ButtonManager::flushAll() {
    for (int i = 0; i < BTN_COUNT; i++) {
        buttons[i].pressedFlag = false;
        buttons[i].stableState    = HIGH;
        buttons[i].lastRawState   = HIGH;
        buttons[i].lastChangeTime = millis();
    }
}

uint8_t ButtonManager::getPin(ButtonID id) {
    return buttons[id].pin;
}