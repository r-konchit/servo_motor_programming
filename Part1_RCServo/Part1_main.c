/*
 * Lab 3, Part 1: pot controls the RC servo, LED bar mirrors the pot.
 *
 * Wiring (Port V):
 *   V-3 (pin 3) -> 1k resistor -> pot wiper (pot across 3.3V and GND)
 *   V-4 (pin 4) -> servo signal (servo red = 5V rail, brown = GND rail)
 *   GND         -> common ground with the ATX supply
 *   LED CONTROL jumper (JP7) on, nothing plugged into Port X (the LEDs use it)
 *
 * Project files: this file + BOARD.c AD.c LED.c RC_Servo.c serial.c from
 * C:\ECE118\src, and C:\ECE118\bootloader320.ld
 */

#include <xc.h>
#include <BOARD.h>
#include <AD.h>
#include <RC_Servo.h>
#include <LED.h>
#include <stdio.h>

#define POT_PIN       AD_PORTV3
#define SERVO_PIN     RC_PORTV04
#define ADC_MAX       1023        // 10-bit ADC: 0V = 0, 3.3V = 1023
#define VREF_MV       3300        // A/D full scale in millivolts
#define NUM_LEDS      12          // 3 banks x 4 LEDs
#define NUM_SAMPLES   128         // A/D readings averaged per pot value (about 27 ms)
#define POT_DEADBAND  4           // ignore changes smaller than this so the servo doesn't twitch

/* At most 10 status lines per second: printing faster than 115200 baud can
 * send overflows serial.c's buffer, which silently drops characters. The core
 * timer counts at 40MHz (half the 80MHz system clock). */
#define PRINT_INTERVAL_TICKS  (40000000 / 10)

/* Average NUM_SAMPLES fresh A/D readings of the pot. The servo's current pulses
 * ripple the shared ground, which shows up as noise on V3. Averaging over more
 * than one 20 ms servo frame cancels most of it. */
static unsigned int ReadPotAveraged(void)
{
    unsigned int sum = 0;
    unsigned int count = 0;
    unsigned int value;

    while (count < NUM_SAMPLES) {
        if (AD_IsNewDataReady()) {
            value = AD_ReadADPin(POT_PIN);
            if (value <= ADC_MAX) {     // skip ERROR readings
                sum += value;
                count++;
            }
        }
    }
    return (sum + NUM_SAMPLES / 2) / NUM_SAMPLES;
}

/* Turn the pot reading into a servo pulse width (microseconds). */
static unsigned int PotToPulse(unsigned int potValue)
{
    return MINPULSE + (potValue * (MAXPULSE - MINPULSE)) / ADC_MAX;
}

/* Light up 0 to 12 LEDs in a bar, proportional to the pot reading, and return
 * how many are lit. The pot range is split into 13 equal steps (0..12 LEDs), so
 * the last LED comes on before the very top of the pot's travel. */
static unsigned int ShowOnLedBar(unsigned int potValue)
{
    unsigned int ledsOn = (potValue * (NUM_LEDS + 1)) / (ADC_MAX + 1);  // 0..12
    unsigned int bar = (1 << ledsOn) - 1;                               // e.g. 5 LEDs -> 0b11111

    LED_SetBank(LED_BANK1, bar & 0x0F);          // LEDs 1-4
    LED_SetBank(LED_BANK2, (bar >> 4) & 0x0F);   // LEDs 5-8
    LED_SetBank(LED_BANK3, (bar >> 8) & 0x0F);   // LEDs 9-12
    return ledsOn;
}

int main(void)
{
    unsigned int potValue;
    unsigned int pulse = 0;
    unsigned int printedPulse = 0;
    unsigned int ledsOn = 0;
    unsigned int millivolts;
    unsigned int lastPrintTime = 0;
    int lastPot = -ADC_MAX;     // far from any real reading, so the first one always updates
    int change;

    BOARD_Init();               // also starts the A/D, so no AD_Init() here

    AD_AddPins(POT_PIN);

    RC_Init();
    RC_AddPins(SERVO_PIN);      // starts centered at 1500 us

    LED_Init();
    LED_AddBanks(LED_BANK1 | LED_BANK2 | LED_BANK3);

    printf("Lab 3 Part 1: servo control\r\n");

    // The A/D adds V3 at its next interrupt and reorders its buffer, so wait for
    // a complete scan that includes V3 before trusting a reading.
    while (!(AD_ActivePins() & POT_PIN)) {
        ;
    }
    AD_IsNewDataReady();        // clears the flag left by the scan from before V3 was added
    while (!AD_IsNewDataReady()) {
        ;
    }

    while (1) {
        potValue = ReadPotAveraged();

        change = (int) potValue - lastPot;
        if ((change >= POT_DEADBAND) || (change <= -POT_DEADBAND)) {
            lastPot = potValue;
            pulse = PotToPulse(potValue);

            RC_SetPulseTime(SERVO_PIN, pulse);
            ledsOn = ShowOnLedBar(potValue);
        }

        // report the latest setting, at most every 100 ms, once the pot has moved
        if ((pulse != printedPulse) && ((_CP0_GET_COUNT() - lastPrintTime) >= PRINT_INTERVAL_TICKS)) {
            millivolts = (lastPot * VREF_MV + ADC_MAX / 2) / ADC_MAX;
            printf("V3 = %u.%02u V (pot %4d)  ->  pulse %4u us, %2u LEDs\r\n",
                    millivolts / 1000, (millivolts % 1000) / 10, lastPot, pulse, ledsOn);
            printedPulse = pulse;
            lastPrintTime = _CP0_GET_COUNT();
        }
    }

    return 0;
}
