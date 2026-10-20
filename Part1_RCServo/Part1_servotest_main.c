/*
 * Lab 3, Part 1 step 1: servo only, no pot.
 *
 * Swings the servo on Port V pin 4 between two positions once a second so you
 * can see it working. Once it moves, Part1_main.c adds the pot.
 *
 * Wiring:
 *   V4 (Port V pin 4) -> servo signal (yellow/orange)
 *   servo red   -> 5V rail
 *   servo brown -> GND rail, which is tied to the Uno32 ground
 *
 * Build: this file + BOARD.c AD.c RC_Servo.c serial.c from C:\ECE118\src,
 * and C:\ECE118\bootloader320.ld
 */

#include <xc.h>
#include <BOARD.h>
#include <RC_Servo.h>
#include <stdio.h>

#define SERVO_PIN    RC_PORTV04
#define POSITION_A   1250        // us, between min (1000) and center (1500)
#define POSITION_B   1750        // us, between center and max (2000)
#define HOLD_TICKS   40000000    // 1 s of the 40MHz core timer

/* Busy-wait for the given number of core timer ticks. */
static void Wait(unsigned int ticks)
{
    unsigned int start = _CP0_GET_COUNT();
    while ((_CP0_GET_COUNT() - start) < ticks) {
        ;
    }
}

int main(void)
{
    unsigned int pulse = POSITION_A;

    BOARD_Init();
    RC_Init();
    RC_AddPins(SERVO_PIN);      // starts centered at 1500 us

    printf("Lab 3 Part 1: servo test on V4\r\n");

    while (1) {
        RC_SetPulseTime(SERVO_PIN, pulse);
        printf("pulse = %u us\r\n", pulse);
        Wait(HOLD_TICKS);
        pulse = (pulse == POSITION_A) ? POSITION_B : POSITION_A;
    }
    return 0;
}
