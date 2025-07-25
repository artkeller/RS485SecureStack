#ifndef RS485_DIRECTION_CONTROL_H
#define RS485_DIRECTION_CONTROL_H

class RS485DirectionControl {
public:
    virtual ~RS485DirectionControl() = default;
    virtual void begin() = 0;
    virtual void setTransmitMode() = 0;
    virtual void setReceiveMode() = 0;
};

#endif // RS485_DIRECTION_CONTROL_H
