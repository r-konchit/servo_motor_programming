/*
 * Lab 3, Part 1 wiring check (not part of the lab)
 *
 * Reads every Port V analog pin (V3-V8) and prints, for each one, the lowest
 * and highest reading seen in the last 200 ms, in A/D counts (0 = 0V,
 * 1023 = 3.3V). The servo output is NOT driven, so nothing couples into V3.
 *
 * Turn the pot slowly and watch which column follows it:
 *   - a pin wired to the pot shows a tight range (a few counts) that moves with the knob
 *   - an unconnected pin shows a wide or wandering range
 *
 * Build: this file + BOARD.c AD.c serial.c, and bootloader320.ld
 */

#include <xc.h>
#include <BOARD.h>
#include <AD.h>
#include <stdio.h>

#define NUM_PINS        6
#define ALL_PORTV_PINS  (AD_PORTV3 | AD_PORTV4 | AD_PORTV5 | AD_PORTV6 | AD_PORTV7 | AD_PORTV8)
#define ADC_MAX         1023
#define WINDOW_TICKS    (40000000 / 5)  // 200 ms of the 40MHz core timer

static const unsigned int pins[NUM_PINS] = {
    AD_PORTV3, AD_PORTV4, AD_PORTV5, AD_PORTV6, AD_PORTV7, AD_PORTV8
};

int main(void)
{
    unsigned int minValue[NUM_PINS], maxValue[NUM_PINS];
    unsigned int value, start, i;

    BOARD_Init();
    AD_AddPins(ALL_PORTV_PINS);

    // wait until the A/D has picked up the new pins and finished a full scan
    while ((AD_ActivePins() & ALL_PORTV_PINS) != ALL_PORTV_PINS) {
        ;
    }
    AD_IsNewDataReady();
    while (!AD_IsNewDataReady()) {
        ;
    }

    printf("\r\nPort V wiring check - turn the pot and see which column follows it.\r\n");
    printf("min-max over 200 ms, A/D counts (0 = 0V, 1023 = 3.3V)\r\n");
    printf("   V3        V4        V5        V6        V7        V8\r\n");

    while (1) {
        for (i = 0; i < NUM_PINS; i++) {
            minValue[i] = ADC_MAX;
            maxValue[i] = 0;
        }
        start = _CP0_GET_COUNT();
        while ((_CP0_GET_COUNT() - start) < WINDOW_TICKS) {
            if (AD_IsNewDataReady()) {
                for (i = 0; i < NUM_PINS; i++) {
                    value = AD_ReadADPin(pins[i]);
                    if (value <= ADC_MAX) {
                        if (value < minValue[i]) {
                            minValue[i] = value;
                        }
                        if (value > maxValue[i]) {
                            maxValue[i] = value;
                        }
                    }
                }
            }
        }
        for (i = 0; i < NUM_PINS; i++) {
            printf("%4u-%-4u ", minValue[i], maxValue[i]);
        }
        printf("\r\n");
    }
    return 0;
}
