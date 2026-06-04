#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>
#include "config.h"

enum ButtonID {
    BTN_ID_PLAY = 0,
    BTN_ID_NEXT,
    BTN_ID_MENU,
    BTN_ID_OK,
    BTN_COUNT
};

class ButtonManager {
public:
    void begin();
    void update();                  // Call every loop iteration
    void flushAll();
    bool pressed(ButtonID id);      // Returns true ONCE per press (falling edge)
    bool isHeld(ButtonID id);       // Returns true while button is held down
    uint8_t getPin(ButtonID id);

private:
    struct Button {
        uint8_t pin;
        bool stableState;           // Debounced state
        bool lastRawState;          // Last raw reading
        unsigned long lastChangeTime;
        bool pressedFlag;           // One-shot flag for edge detection
    };

    Button buttons[BTN_COUNT];
};

#endif