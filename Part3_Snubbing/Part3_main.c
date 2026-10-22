/*
 * Lab 3, Part 3: snubbing the inductive kickback.
 *
 * Same hardware, pins and pot control as Part 2: the pot on V3 sets the PWM
 * duty on Y12, which drives the DS3658, and the LED bar shows the duty.
 * Keyboard commands set up each measurement the lab asks for, to be repeated
 * for every snubber arrangement:
 *
 *   l / m / h   very low (5%), intermediate (50%), very high (95%) duty
 *   1-9         hold 10%..90% duty           0      back to pot control
 *   + / -       duty +1% / -1%               > / <  duty +0.1% / -0.1%
 *   f / F       next / previous PWM frequency, 100 Hz .. 100 kHz
 *   s           sweep 0% -> 100% in 10% steps, 3 s each (speed vs duty)
 *   ?           list the keys
 *
 * Wiring: same as Part 2. Change snubber parts ONLY with the power off, and
 * never run the motor with no snubber at all (DS3658 unclamped and nothing
 * external) - the kickback can destroy the DS3658.
 *
 * Scope: probe the DS3658 output (the motor's - lead) with the ground clip on
 * GROUND, never on the collector. Motor voltage = supply - collector voltage.
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
#define MOTOR_PWM       PWM_PORTY12     // same pin as Part 2
#define MOTOR_PWM_LAT   PORTY12_LAT
#define MOTOR_PWM_TRIS  PORTY12_TRIS

#define ADC_MAX         1023        // 10-bit ADC: 0V = 0, 3.3V = 1023
#define NUM_SAMPLES     128         // A/D readings averaged per pot value (about 27 ms)
#define ADC_DEADBAND    4           // counts at each end snapped to exactly 0% / 100%
#define DUTY_HYSTERESIS 3           // per mille; keeps leftover noise from jittering the duty
#define NUM_LEDS        12          // 3 banks x 4 LEDs

#define POT_CONTROL     -1          // fixedDuty value meaning "follow the pot"

#define VERY_LOW_DUTY   50          // 5.0%
#define MID_DUTY        500         // 50.0%
#define VERY_HIGH_DUTY  950         // 95.0%

#define SWEEP_STEP      100         // 10% per sweep step
#define SWEEP_HOLD_TICKS (40000000 * 3)    // 3 s per step (core timer runs at 40MHz)

/* At most 10 status lines per second, so serial.c's buffer never overflows. */
#define PRINT_INTERVAL_TICKS  (40000000 / 10)

static const unsigned int frequencies[] = {
    100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000
};
#define NUM_FREQUENCIES (sizeof(frequencies) / sizeof(frequencies[0]))
#define START_FREQUENCY_INDEX 3     // 1 kHz, the same as Part 2

static int fixedDuty = POT_CONTROL;
static unsigned int duty = 0;           // per mille, 0..MAX_PWM (1000)
static unsigned int freqIndex = START_FREQUENCY_INDEX;
static int sweeping = FALSE;
static unsigned int sweepStepTime;

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

/* Change the PWM frequency. pwm.c scales the duty register to the old period,
 * so the duty has to be written again afterwards. */
static void SetFrequency(unsigned int index)
{
    freqIndex = index;
    PWM_SetFrequency(frequencies[freqIndex]);
    PWM_SetDutyCycle(MOTOR_PWM, duty);
}

/* Step the held duty by delta per mille, starting from the present duty. */
static void NudgeDuty(int delta)
{
    int newDuty = (int) duty + delta;
    if (newDuty < 0) {
        newDuty = 0;
    }
    if (newDuty > MAX_PWM) {
        newDuty = MAX_PWM;
    }
    fixedDuty = newDuty;
}

static void StartSweep(void)
{
    sweeping = TRUE;
    fixedDuty = 0;
    sweepStepTime = _CP0_GET_COUNT();
    printf("sweep: 0%% to 100%% in 10%% steps, 3 s each (any key stops)\r\n");
}

/* Advance the sweep one step every SWEEP_HOLD_TICKS; stop at 0% after 100%. */
static void UpdateSweep(void)
{
    if ((_CP0_GET_COUNT() - sweepStepTime) < SWEEP_HOLD_TICKS) {
        return;
    }
    sweepStepTime = _CP0_GET_COUNT();
    if (fixedDuty >= MAX_PWM) {
        sweeping = FALSE;
        fixedDuty = 0;
        printf("sweep done\r\n");
    } else {
        fixedDuty += SWEEP_STEP;
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
    printf("Keys:  l m h  very low (5%%) / intermediate (50%%) / very high (95%%) duty\r\n");
    printf("       1-9    hold 10%%..90%% duty      0    back to pot control\r\n");
    printf("       + -    duty +1%% / -1%%           > <  duty +0.1%% / -0.1%%\r\n");
    printf("       f F    next / previous PWM frequency (100 Hz .. 100 kHz)\r\n");
    printf("       s      sweep 0-100%% in 10%% steps, 3 s each\r\n");
}

static void HandleKey(char key)
{
    if (sweeping) {             // any key ends a sweep, then does its usual job
        sweeping = FALSE;
        printf("sweep stopped\r\n");
    }

    switch (key) {
    case '0': fixedDuty = POT_CONTROL;
        break;
    case 'l': fixedDuty = VERY_LOW_DUTY;
        break;
    case 'm': fixedDuty = MID_DUTY;
        break;
    case 'h': fixedDuty = VERY_HIGH_DUTY;
        break;
    case '+': NudgeDuty(10);
        break;
    case '-': NudgeDuty(-10);
        break;
    case '>': NudgeDuty(1);
        break;
    case '<': NudgeDuty(-1);
        break;
    case 'f': SetFrequency((freqIndex + 1) % NUM_FREQUENCIES);
        break;
    case 'F': SetFrequency((freqIndex + NUM_FREQUENCIES - 1) % NUM_FREQUENCIES);
        break;
    case 's': StartSweep();
        break;
    case '?': PrintHelp();
        break;
    default:
        if ((key >= '1') && (key <= '9')) {
            fixedDuty = (key - '0') * 100;
        }
        break;
    }
}

int main(void)
{
    char key;
    unsigned int pot;
    unsigned int newDuty;
    int mode;
    unsigned int printedDuty = MAX_PWM + 1;     // forces the first status line
    int printedMode = -1;
    unsigned int printedFreq = NUM_FREQUENCIES;
    unsigned int lastPrintTime = 0;

    BOARD_Init();               // also starts the A/D, so no AD_Init() here
    AD_AddPins(POT_PIN);

    LED_Init();
    LED_AddBanks(LED_BANK1 | LED_BANK2 | LED_BANK3);
    ShowOnLedBar(0);

    MOTOR_PWM_LAT = 0;          // hold the DS3658 input low until the PWM module takes the pin
    MOTOR_PWM_TRIS = 0;
    PWM_Init();
    PWM_AddPins(MOTOR_PWM);     // starts at 0% duty
    SetFrequency(START_FREQUENCY_INDEX);

    printf("Lab 3 Part 3: snubbing - DS3658 PWM on Y12, pot on V3\r\n");
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
        if (sweeping) {
            UpdateSweep();
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

        // report frequency and duty, at most every 100 ms, whenever anything changes
        mode = sweeping ? 2 : ((fixedDuty == POT_CONTROL) ? 0 : 1);
        if (((duty != printedDuty) || (mode != printedMode) || (freqIndex != printedFreq))
                && ((_CP0_GET_COUNT() - lastPrintTime) >= PRINT_INTERVAL_TICKS)) {
            printf("freq = %6u Hz  duty = %3u.%u%%  %s\r\n", frequencies[freqIndex],
                    duty / 10, duty % 10,
                    (mode == 0) ? "(pot)" : ((mode == 1) ? "(fixed)" : "(sweep)"));
            printedDuty = duty;
            printedMode = mode;
            printedFreq = freqIndex;
            lastPrintTime = _CP0_GET_COUNT();
        }
    }

    return 0;
}
