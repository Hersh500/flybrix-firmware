/*
    *  Flybrix Flight Controller -- Copyright 2015 Flying Selfie Inc.
    *
    *  License and other details available at: http://www.flybrix.com/firmware
*/

#include "control.h"
#include "debug.h"
#include "state.h"

#define BYPASS_THRUST_MASTER 1 << 0
#define BYPASS_PITCH_MASTER 1 << 1
#define BYPASS_ROLL_MASTER 1 << 2
#define BYPASS_YAW_MASTER 1 << 3
#define BYPASS_THRUST_SLAVE 1 << 4
#define BYPASS_PITCH_SLAVE 1 << 5
#define BYPASS_ROLL_SLAVE 1 << 6
#define BYPASS_YAW_SLAVE 1 << 7

// PID parameters:

// MASTER:
// P
// I
// D
// Windup guard
// D filter usec (15Hz)
// setpoint filter usec (30Hz)
// (meters / full stick action)

// SLAVE:
// P
// I
// D
// Windup guard
// D filter usec (150Hz)
// setpoint filter usec (300Hz)
// (deg/sec / full stick action)

Control::PIDParameters::PIDParameters()
    : thrust_master{1.0, 0.0, 0.0, 0.0, 0.005, 0.005, 1.0},
      pitch_master{10.0, 1.0, 0.0, 10.0, 0.005, 0.005, 10.0},
      roll_master{10.0, 1.0, 0.0, 10.0, 0.005, 0.005, 10.0},
      yaw_master{5.0, 1.0, 0.0, 10.0, 0.005, 0.005, 180.0},
      thrust_slave{1.0, 0.0, 0.0, 10.0, 0.001, 0.001, 0.3},
      pitch_slave{10.0, 4.0, 0.0, 30.0, 0.001, 0.001, 30.0},
      roll_slave{10.0, 4.0, 0.0, 30.0, 0.001, 0.001, 30.0},
      yaw_slave{30.0, 5.0, 0.0, 20.0, 0.001, 0.001, 240.0},
      thrust_gain{4095.0},
      pitch_gain{2047.0},
      roll_gain{2047.0},
      yaw_gain{2047.0},
      pid_bypass{BYPASS_THRUST_MASTER | BYPASS_THRUST_SLAVE | BYPASS_YAW_MASTER}  // AHRS/Horizon mode
{
}

namespace {
enum PID_ID {
    THRUST_MASTER = 0,
    PITCH_MASTER = 1,
    ROLL_MASTER = 2,
    YAW_MASTER = 3,
    THRUST_SLAVE = 4,
    PITCH_SLAVE = 5,
    ROLL_SLAVE = 6,
    YAW_SLAVE = 7,
};
}

Control::Control(State* __state, const PIDParameters& config)
    : state(__state),
      pid_parameters(config),
      thrust_pid{config.thrust_master, config.thrust_slave},
      pitch_pid{config.pitch_master, config.pitch_slave},
      roll_pid{config.roll_master, config.roll_slave},
      yaw_pid{config.yaw_master, config.yaw_slave} {
    parseConfig();
}

bool Control::PIDParameters::verify() const {
    bool retval{true};
    if (!(pid_bypass & (1 << THRUST_SLAVE))) {
        // If the thrust slave is enabled
        DebugPrint("The slave PID of the thrust regulator must be disabled for now");
        retval = false;
    }
    return retval;
}

void Control::parseConfig() {
    thrust_pid = {pid_parameters.thrust_master, pid_parameters.thrust_slave};
    pitch_pid = {pid_parameters.pitch_master, pid_parameters.pitch_slave};
    roll_pid = {pid_parameters.roll_master, pid_parameters.roll_slave};
    yaw_pid = {pid_parameters.yaw_master, pid_parameters.yaw_slave};

    for (uint8_t i = 0; i < 8; ++i)
        pidEnabled[i] = ((pid_parameters.pid_bypass & (1 << i)) == 0);

    // all slaves are rate controllers; set up the master pids as wrapped angle controllers
    pitch_pid.isMasterWrapped();
    roll_pid.isMasterWrapped();
    yaw_pid.isMasterWrapped();

    thrust_pid.IntegralReset();
    pitch_pid.IntegralReset();
    roll_pid.IntegralReset();
    yaw_pid.IntegralReset();
}

void Control::calculateControlVectors() {
    thrust_pid.setMasterInput(state->kinematicsAltitude);
    thrust_pid.setSlaveInput(0.0f);  // state->kinematicsClimbRate
    pitch_pid.setMasterInput(state->kinematicsAngle[0] * 57.2957795f);
    pitch_pid.setSlaveInput(state->kinematicsRate[0] * 57.2957795f);
    roll_pid.setMasterInput(state->kinematicsAngle[1] * 57.2957795f);
    roll_pid.setSlaveInput(state->kinematicsRate[1] * 57.2957795f);
    yaw_pid.setMasterInput(state->kinematicsAngle[2] * 57.2957795f);
    yaw_pid.setSlaveInput(state->kinematicsRate[2] * 57.2957795f);

    thrust_pid.setSetpoint(state->command_throttle * (1.0f / 4095.0f) * thrust_pid.getScalingFactor(pidEnabled[THRUST_MASTER], pidEnabled[THRUST_SLAVE], pid_parameters.thrust_gain));
    pitch_pid.setSetpoint(state->command_pitch * (1.0f / 2047.0f) * pitch_pid.getScalingFactor(pidEnabled[PITCH_MASTER], pidEnabled[PITCH_SLAVE], pid_parameters.pitch_gain));
    roll_pid.setSetpoint(state->command_roll * (1.0f / 2047.0f) * roll_pid.getScalingFactor(pidEnabled[ROLL_MASTER], pidEnabled[ROLL_SLAVE], pid_parameters.roll_gain));
    yaw_pid.setSetpoint(state->command_yaw * (1.0f / 2047.0f) * yaw_pid.getScalingFactor(pidEnabled[YAW_MASTER], pidEnabled[YAW_SLAVE], pid_parameters.yaw_gain));

    // compute new output levels for state
    uint32_t now = micros();
    state->Fz = thrust_pid.Compute(now, pidEnabled[THRUST_MASTER], pidEnabled[THRUST_SLAVE]);
    state->Tx = pitch_pid.Compute(now, pidEnabled[PITCH_MASTER], pidEnabled[PITCH_SLAVE]);
    state->Ty = roll_pid.Compute(now, pidEnabled[ROLL_MASTER], pidEnabled[ROLL_SLAVE]);
    state->Tz = yaw_pid.Compute(now, pidEnabled[YAW_MASTER], pidEnabled[YAW_SLAVE]);

    if (state->Fz == 0) {  // throttle is in low condition
        state->Tx = 0;
        state->Ty = 0;
        state->Tz = 0;

        thrust_pid.IntegralReset();
        pitch_pid.IntegralReset();
        roll_pid.IntegralReset();
        yaw_pid.IntegralReset();
    }
}
