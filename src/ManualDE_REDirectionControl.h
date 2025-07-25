#ifndef MANUAL_DE_RE_DIRECTION_CONTROL_H
#define MANUAL_DE_RE_DIRECTION_CONTROL_H

#include "RS485DirectionControl.h"
#include <Arduino.h>

class ManualDE_REDirectionControl : public RS485DirectionControl {
private:
    int _deRePin;

public:
    ManualDE_REDirectionControl(int deRePin) : _deRePin(deRePin) {}

    void begin() override {
        pinMode(_deRePin, OUTPUT);
        setReceiveMode();
    }

    void setTransmitMode() override {
        digitalWrite(_deRePin, HIGH);
    }

    void setReceiveMode() override {
        digitalWrite(_deRePin, LOW);
    }
};

#endif // MANUAL_DE_RE_DIRECTION_CONTROL_H
