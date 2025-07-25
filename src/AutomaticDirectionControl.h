#ifndef AUTOMATIC_DIRECTION_CONTROL_H
#define AUTOMATIC_DIRECTION_CONTROL_H

#include "RS485DirectionControl.h"

class AutomaticDirectionControl : public RS485DirectionControl {
public:
    AutomaticDirectionControl() {}
    void begin() override {}
    void setTransmitMode() override {}
    void setReceiveMode() override {}
};

#endif // AUTOMATIC_DIRECTION_CONTROL_H
