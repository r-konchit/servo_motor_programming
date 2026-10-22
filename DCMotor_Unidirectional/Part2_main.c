/*
 * Lab 3, Part 2: unidirectional DC motor drive through the DS3658.
 *
 * The pot on V3 sets the PWM duty cycle on Y12, which drives a DS3658 input
 * (a low-side switch). 0V = 0% duty and no LEDs, 3.3V = 100% duty and the full
 * LED bar. Number keys hold a fixed duty for the 20% / 80% scope captures.
 *
 * Wiring:
 *   V-3 (Port V pin 3)  -> 1k resistor -> pot wiper (same pot circuit as Part 1)
 *   Y-12 (Port Y pin 12) -> DS3658 input (PWM, 3.3V logic) - see the DS3658 doc for the pin
 *   Motor +  -> power distribution board (+)
 *   Motor -  -> DS3658 output for that input
 *   DS3658 jumpered for CLAMPED mode
 *   Grounds: DS3658, pot (-) rail and the Uno32 all tied together
 *   LED CONTROL jumper (JP7) on, nothing plugged into Port X (the LEDs use it)
 *
 * Scope Y12 (duty follows the pot) BEFORE connecting the DS3658.
 *
 * Project files: this file + BOARD.c AD.c LED.c pwm.c serial.c from
 * C:\ECE118\src, and C:\ECE118\bootloader320.ld
 */

#include <xc.h>
#include <BOARD.h>
#include <AD.h>
#include <IO_Ports.h>
#include <LED.h>
#include <pwm.h>
#include <serial.h>
#include <stdio.h>

#define POT_PIN         AD_PORTV3
#define MOTOR_PWM       PWM_PORTY12     // PWM-capable pins: Z06 Y12 Y10 X11 (Y04 is an LED)
#define MOTOR_PWM_LAT   PORTY12_LAT
#define MOTOR_PWM_TRIS  PORTY12_TRIS

#define ADC_MAX         1023        // 10-bit ADC: 0V = 0, 3.3V = 1023
#define NUM_SAMPLES     128         // A/D readings averaged per pot value (about 27 ms)
#define ADC_DEADBAND    4           // counts at each end snapped to exactly 0% / 100%
#define DUTY_HYSTERESIS 3           // per mille; keeps leftover noise from jittering the duty
#define NUM_LEDS        12          // 3 banks x 4 LEDs

#define POT_CONTROL     -1          // fixedDuty value meaning "follow the pot"

/* At most 10 status lines per second, so serial.c's buffer never overflows.
 * The core timer counts at 40MHz (half the 80MHz system clock). */
#define PRINT_INTERVAL_TICKS  (40000000 / 10)

static int fixedDuty = POT_CONTROL;
static unsigned int duty = 0;           // per mille, 0..MAX_PWM (1000)

/* Average NUM_SAMPLES fresh A/D readings of the pot. Motor current ripples the
 * shared ground, which shows up as noise on V3; averaging cancels most of it. */
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

/* Pot reading -> duty in per mille, with the ends snapped to exactly 0% and 100%. */
static unsigned int PotToDuty(unsigned int pot)
{
    if (pot <= ADC_DEADBAND) {
        return 0;
    }
    if (pot >= ADC_MAX - ADC_DEADBAND) {
        return MAX_PWM;
    }
    return (pot * MAX_PWM + ADC_MAX / 2) / ADC_MAX;
}

/* Pot changes smaller than DUTY_HYSTERESIS are ignored, except reaching 0% or 100%. */
static int PotDutyChanged(unsigned int newDuty)
{
    int change = (int) newDuty - (int) duty;
    return (change >= DUTY_HYSTERESIS) || (change <= -DUTY_HYSTERESIS)
            || ((newDuty != duty) && ((newDuty == 0) || (newDuty == MAX_PWM)));
}

static void SetDuty(unsigned int newDuty)
{
    if (newDuty != duty) {
        duty = newDuty;
        PWM_SetDutyCycle(MOTOR_PWM, duty);
    }
}

/* Light up 0 to 12 LEDs in a bar, proportional to the duty cycle. The range is
 * split into 13 equal steps, so all 12 are lit only near 100%. */
static void ShowOnLedBar(unsigned int dutyCycle)
{
    unsigned int ledsOn = (dutyCycle * (NUM_LEDS + 1)) / (MAX_PWM + 1);  // 0..12
    unsigned int bar = (1 << ledsOn) - 1;                               // e.g. 5 LEDs -> 0b11111

    LED_SetBank(LED_BANK1, bar & 0x0F);          // LEDs 1-4
    LED_SetBank(LED_BANK2, (bar >> 4) & 0x0F);   // LEDs 5-8
    LED_SetBank(LED_BANK3, (bar >> 8) & 0x0F);   // LEDs 9-12
}

static void PrintHelp(void)
{
    printf("Keys:  1-9  hold 10%%..90%% duty (2 and 8 for the 20%%/80%% captures)\r\n");
    printf("       0    back to pot control\r\n");
}

static void HandleKey(char key)
{
    if ((key >= '1') && (key <= '9')) {
        fixedDuty = (key - '0') * 100;
    } else if (key == '0') {
        fixedDuty = POT_CONTROL;
    } else if (key == '?') {
        PrintHelp();
    }
}

int main(void)
{
    char key;
    unsigned int pot;
    unsigned int newDuty;
    unsigned int printedDuty = MAX_PWM + 1;    // forces the first status line
    int printedMode = 0;
    unsigned int lastPrintTime = 0;

    BOARD_Init();               // also starts the A/D, so no AD_Init() here
    AD_AddPins(POT_PIN);

    LED_Init();
    LED_AddBanks(LED_BANK1 | LED_BANK2 | LED_BANK3);
    ShowOnLedBar(0);

    MOTOR_PWM_LAT = 0;          // hold the DS3658 input low until the PWM module takes the pin
    MOTOR_PWM_TRIS = 0;
    PWM_Init();                 // 1 kHz
    PWM_AddPins(MOTOR_PWM);     // starts at 0% duty

    printf("Lab 3 Part 2: DS3658 PWM on Y12 at %u Hz, pot on V3\r\n", PWM_GetFrequency());
    PrintHelp();

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
        key = GetChar();
        if (key) {
            HandleKey(key);
        }

        if (fixedDuty != POT_CONTROL) {
            SetDuty(fixedDuty);
        } else {
            pot = ReadPotAveraged();
            newDuty = PotToDuty(pot);
            if (PotDutyChanged(newDuty)) {
                SetDuty(newDuty);
            }
        }
        ShowOnLedBar(duty);

        // report the duty, at most every 100 ms, whenever it or the mode changes
        if (((duty != printedDuty) || (fixedDuty != printedMode))
                && ((_CP0_GET_COUNT() - lastPrintTime) >= PRINT_INTERVAL_TICKS)) {
            printf("duty = %3u.%u%%  %s\r\n", duty / 10, duty % 10,
                    (fixedDuty == POT_CONTROL) ? "(pot)" : "(fixed)");
            printedDuty = duty;
            printedMode = fixedDuty;
            lastPrintTime = _CP0_GET_COUNT();
        }
    }

    return 0;
}
