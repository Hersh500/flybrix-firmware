/*
    *  Flybrix Flight Controller -- Copyright 2015 Flying Selfie Inc.
    *
    *  License and other details available at: http://www.flybrix.com/firmware

    <config.h/cpp>

    EEPROM based configuration data storage structure

    Nonvolatile parameters are being stored inside an CONFIG structure
    that can be accesed as data union, for easier manipulation as a javascript
    ArrayBuffer object over serial.

*/

#ifndef config_h
#define config_h

#include <Arduino.h>
#include <EEPROM.h>

#include "AK8963.h"
#include "R415X.h"
#include "airframe.h"
#include "control.h"
#include "led.h"
#include "state.h"
#include "version.h"

struct Systems;

struct __attribute__((packed)) ConfigID {
    ConfigID();
    explicit ConfigID(uint32_t id) : id{id} {
    }
    bool verify() const {
        return true;
    }
    uint32_t id;
};

static_assert(sizeof(ConfigID) == 4, "Data is not packed");

struct __attribute__((packed)) PcbTransform {
    PcbTransform();
    bool verify() const {
        return true;
    }
    float orientation[3];  // pitch/roll/yaw in standard flyer coordinate system
                           // --> applied in that order!
    float translation[3];  // translation in standard flyer coordinate system
};

static_assert(sizeof(PcbTransform) == 3 * 2 * 4, "Data is not packed");

struct __attribute__((packed)) CONFIG_struct {
    enum Field : uint16_t {
        VERSION = 1 << 0,
        ID = 1 << 1,
        PCB = 1 << 2,
        MIX_TABLE = 1 << 3,
        MAG_BIAS = 1 << 4,
        CHANNEL = 1 << 5,
        PID_PARAMETERS = 1 << 6,
        STATE_PARAMETERS = 1 << 7,
        LED_STATES = 1 << 8,
    };

    CONFIG_struct();
    explicit CONFIG_struct(Systems& sys);
    void applyTo(Systems& systems) const;
    bool verify() const;

    void resetPartial(uint16_t submask, uint16_t led_mask);

    template <class Cursor>
    void writeTo(Cursor&& cursor) const;

    template <class Cursor>
    void writePartialTo(Cursor&& cursor, uint16_t submask, uint16_t led_mask) const;

    template <class Cursor>
    bool readFrom(Cursor&& cursor);

    template <class Cursor>
    bool readPartialFrom(Cursor&& cursor);

    template <class Cursor>
    static bool readMasks(Cursor&& cursor, uint16_t& submask, uint16_t& led_mask);

    Version version;
    ConfigID id;
    PcbTransform pcb;
    Airframe::MixTable mix_table;
    AK8963::MagBias mag_bias;
    R415X::ChannelProperties channel;
    Control::PIDParameters pid_parameters;
    State::Parameters state_parameters;
    LED::States led_states;
};

static_assert(sizeof(CONFIG_struct) ==
                  sizeof(Version) + sizeof(ConfigID) + sizeof(PcbTransform) + sizeof(Airframe::MixTable) + sizeof(AK8963::MagBias) + sizeof(R415X::ChannelProperties) + sizeof(State::Parameters) +
                      sizeof(Control::PIDParameters) + sizeof(LED::States),
              "Data is not packed");

static_assert(sizeof(CONFIG_struct) == 619, "Data does not have expected size");

CONFIG_struct readEEPROM();
bool isEmptyEEPROM();

#include "config_impl.h"

#endif
