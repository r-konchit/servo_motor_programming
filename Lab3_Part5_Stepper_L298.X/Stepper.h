/* Lab 3 stepper module, extended from the supplied Stepper.h interface.
 * Select exactly one mode. The L298 modes use PORT Z pins 3,4,5,7,8,9.
 * The DRV8811 mode uses Z4 STEP, Z3 DIR, Z5 ENABLE.
 */
#ifndef LAB3_STEPPER_H
#define LAB3_STEPPER_H

#include <stdint.h>
#include <BOARD.h>

/* The default mode is full step. Define one of the other three mode names
 * in the MPLAB project preprocessor settings when testing that mode. */
#if !defined(FULL_STEP_DRIVE) && !defined(HALF_STEP_DRIVE) && \
    !defined(WAVE_DRIVE) && !defined(DRV8811_DRIVE)
#define FULL_STEP_DRIVE
#endif

#if (defined(FULL_STEP_DRIVE) + defined(HALF_STEP_DRIVE) + \
     defined(WAVE_DRIVE) + defined(DRV8811_DRIVE)) != 1
#error "Select exactly one stepper drive mode in Stepper.h"
#endif

#define FORWARD 1u
#define REVERSE 0u

int8_t Stepper_Init(void);
int8_t Stepper_SetRate(uint16_t rate); /* 0 selects 0.5 steps/s; 1..20000 otherwise */
uint16_t Stepper_GetRate(void);
int8_t Stepper_SetSteps(uint8_t direction, int32_t steps);
int8_t Stepper_InitSteps(uint8_t direction, int32_t steps);
int8_t Stepper_StartSteps(void);
int8_t Stepper_StopsSteps(void); /* Name retained from supplied interface. */
int32_t Stepper_GetRemainingSteps(void);
int8_t Stepper_GetDirection(void);
int8_t Stepper_IsStepping(void);
int8_t Stepper_End(void);

#endif
