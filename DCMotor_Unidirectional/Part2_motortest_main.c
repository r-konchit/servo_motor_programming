/*
 * Lab 3, Part 2 wiring test (not part of the lab)
 *
 * Drives Port Y pin 12 as a plain on/off output: high (motor ON) for 2 s, then
 * low (motor OFF) for 2 s, forever. No pot, no PWM. All 12 LEDs light while the
 * output is high, so you can see the program running without the terminal.
 *
 * The slow switching can be followed with a multimeter from Port Y pin 12, to
 * the DS3658 input, to the DS3658 output (the motor's black wire).
 *
 * Build: this file + BOARD.c AD.c LED.c serial.c, and bootloader320.ld
 */

#include <xc.h>
#include <BOARD.h>
#include <IO_Ports.h>
#include <LED.h>
#include <stdio.h>

#define MOTOR_LAT    PORTY12_LAT
#define MOTOR_TRIS   PORTY12_TRIS
#define HOLD_TICKS   80000000    // 2 s of the 40MHz core timer

static void Wait(unsigned int ticks)
{
    unsigned int start = _CP0_GET_COUNT();
    while ((_CP0_GET_COUNT() - start) < ticks) {
        ;
    }
}

static void SetAllLeds(unsigned char pattern)
{
    LED_SetBank(LED_BANK1, pattern);
    LED_SetBank(LED_BANK2, pattern);
    LED_SetBank(LED_BANK3, pattern);
}

int main(void)
{
    BOARD_Init();
    LED_Init();
    LED_AddBanks(LED_BANK1 | LED_BANK2 | LED_BANK3);

    MOTOR_LAT = 0;
    MOTOR_TRIS = 0;             // Port Y pin 12 as a digital output

    printf("Lab 3 Part 2 wiring test: Port Y pin 12 on 2 s / off 2 s\r\n");

    while (1) {
        MOTOR_LAT = 1;
        SetAllLeds(0x0F);
        printf("ON   - Port Y pin 12 = 3.3 V, motor should run\r\n");
        Wait(HOLD_TICKS);

        MOTOR_LAT = 0;
        SetAllLeds(0x00);
        printf("OFF  - Port Y pin 12 = 0 V, motor should stop\r\n");
        Wait(HOLD_TICKS);
    }
    return 0;
}
