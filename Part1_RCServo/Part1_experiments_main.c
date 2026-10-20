/*
 * File:   Part1_experiments_main.c
 * Lab 3, Part 1 - Driving an RC servo (experiments version)
 *
 * Use this in place of Part1_main.c (never both in one project: two main()s)
 * for the minimum-pulse-change, range and speed measurements.
 *
 * The potentiometer on V3 sets the servo pulse width (MINPULSE..MAXPULSE) on
 * V4, and the LED bar shows the commanded position. Keyboard commands switch to
 * manual or sweep mode for the resolution, range and speed experiments.
 *
 * Wiring (Port V ribbon cable to the breadboard):
 *   V3  <- pot wiper (analog in, must stay within 0-3.3V)
 *   V4  -> servo signal (yellow)
 *   servo power (orange/red) <- regulated 5V from the ATX supply, NOT more
 *   servo ground (brown) and the pot ground share a common ground with the stack
 *   LED CONTROL jumper (JP7) installed; nothing on Port X (the LED banks use it)
 *
 * Check V4 on the scope before connecting the servo: 50Hz-ish frame, high time
 * between 1.0 and 2.0 ms.
 *
 * Project files: this file, plus BOARD.c AD.c LED.c RC_Servo.c serial.c timers.c
 * from C:\ECE118\src, and C:\ECE118\bootloader320.ld
 */

#include <xc.h>
#include <stdio.h>
#include <BOARD.h>
#include <AD.h>
#include <LED.h>
#include <RC_Servo.h>
#include <serial.h>
#include <timers.h>

/*******************************************************************************
 * PRIVATE #DEFINES                                                            *
 ******************************************************************************/
#define POT_PIN         AD_PORTV3
#define SERVO_PIN       RC_PORTV04

#define ADC_MAX         1023
#define NUM_LEDS        12

#define CENTER_PULSE    ((MINPULSE + MAXPULSE) / 2)
#define PULSE_RANGE     (MAXPULSE - MINPULSE)

#define SWEEP_TICK_MS       20      // one sweep update per servo frame
#define DEFAULT_SWEEP_RATE  1000    // us of pulse width per second (full range in 1s)
#define MAX_SWEEP_RATE      64000
#define STATUS_MS           200     // minimum time between status lines

/*******************************************************************************
 * PRIVATE VARIABLES                                                           *
 ******************************************************************************/
typedef enum {
    MODE_POT, MODE_MANUAL, MODE_SWEEP
} ServoMode_t;

static const char *modeNames[] = {"POT", "MANUAL", "SWEEP"};

static ServoMode_t mode = MODE_POT;
static uint16_t pulse = CENTER_PULSE;   // commanded pulse width in us
static uint16_t ledPattern = 0xFFFF;    // LED_AddBanks() turns every LED on; force the first write

static uint32_t sweepRate = DEFAULT_SWEEP_RATE;
static int32_t sweepPos;                // ns, so slow sweeps can move < 1us per tick
static int8_t sweepDir = 1;
static uint32_t lastSweepTime;

/*******************************************************************************
 * PRIVATE FUNCTIONS                                                           *
 ******************************************************************************/

// Sets all 12 LEDs from one pattern: bits 0-3 bank 1, 4-7 bank 2, 8-11 bank 3
static void SetLedBar(uint16_t pattern)
{
    if (pattern == ledPattern) {
        return;
    }
    ledPattern = pattern;
    LED_SetBank(LED_BANK1, pattern & 0x0F);
    LED_SetBank(LED_BANK2, (pattern >> 4) & 0x0F);
    LED_SetBank(LED_BANK3, (pattern >> 8) & 0x0F);
}

// Bar graph pattern: value 0..fullScale lights 0..numLeds LEDs from bit 0 up
static uint16_t BarPattern(uint32_t value, uint32_t fullScale, uint8_t numLeds)
{
    uint32_t lit = (value * (numLeds + 1)) / (fullScale + 1);
    return (uint16_t) ((1u << lit) - 1);
}

// Latest pot reading, or -1 until the A/D has converted the pin
static int16_t ReadPot(void)
{
    unsigned int raw = AD_ReadADPin(POT_PIN);
    if (raw > ADC_MAX) {
        return -1;
    }
    return raw;
}

static void SetPulse(int32_t newPulse)
{
    if (newPulse < MINPULSE) {
        newPulse = MINPULSE;
    }
    if (newPulse > MAXPULSE) {
        newPulse = MAXPULSE;
    }
    if (newPulse != pulse) {
        pulse = newPulse;
        RC_SetPulseTime(SERVO_PIN, pulse);
    }
}

// Manual-mode nudge: any pulse key leaves pot/sweep mode and holds the result
static void NudgePulse(int32_t delta)
{
    mode = MODE_MANUAL;
    SetPulse((int32_t) pulse + delta);
}

static void UpdateSweep(void)
{
    uint32_t now = TIMERS_GetTime();
    if ((now - lastSweepTime) < SWEEP_TICK_MS) {
        return;
    }
    lastSweepTime = now;
    sweepPos += sweepDir * (int32_t) (sweepRate * SWEEP_TICK_MS); // (us/s) * ms = ns
    if (sweepPos >= MAXPULSE * 1000L) {
        sweepPos = MAXPULSE * 1000L;
        sweepDir = -1;
    } else if (sweepPos <= MINPULSE * 1000L) {
        sweepPos = MINPULSE * 1000L;
        sweepDir = 1;
    }
    SetPulse(sweepPos / 1000);
}

static void PrintHelp(void)
{
    printf("\r\nKeys:\r\n");
    printf("  p     pot mode (default)\r\n");
    printf("  m     manual mode (hold current pulse)\r\n");
    printf("  + -   pulse +1 / -1 us     ] [   +10 / -10 us     } {   +100 / -100 us\r\n");
    printf("  c l h center / min (%u us) / max (%u us) - jump between l and h to time a full swing\r\n",
            MINPULSE, MAXPULSE);
    printf("  s     sweep min<->max;  > <  sweep rate x2 / /2\r\n");
    printf("  ?     this help\r\n\r\n");
}

static void HandleKey(char key)
{
    switch (key) {
    case 'p': mode = MODE_POT;
        break;
    case 'm': mode = MODE_MANUAL;
        break;
    case '+': NudgePulse(1);
        break;
    case '-': NudgePulse(-1);
        break;
    case ']': NudgePulse(10);
        break;
    case '[': NudgePulse(-10);
        break;
    case '}': NudgePulse(100);
        break;
    case '{': NudgePulse(-100);
        break;
    case 'c': NudgePulse(CENTER_PULSE - (int32_t) pulse);
        break;
    case 'l': NudgePulse(MINPULSE - (int32_t) pulse);
        break;
    case 'h': NudgePulse(MAXPULSE - (int32_t) pulse);
        break;
    case 's':
        mode = MODE_SWEEP;
        sweepPos = pulse * 1000L;
        lastSweepTime = TIMERS_GetTime();
        break;
    case '>':
        if (sweepRate < MAX_SWEEP_RATE) {
            sweepRate *= 2;
        }
        printf("sweep rate = %lu us/s\r\n", (unsigned long) sweepRate);
        break;
    case '<':
        if (sweepRate > 1) {
            sweepRate /= 2;
        }
        printf("sweep rate = %lu us/s\r\n", (unsigned long) sweepRate);
        break;
    case '?': PrintHelp();
        break;
    default:
        break;
    }
}

static void PrintStatus(int16_t pot)
{
    static uint16_t lastPulse = 0;
    static ServoMode_t lastMode = MODE_POT;
    static uint32_t lastPrint = 0;
    uint32_t now = TIMERS_GetTime();

    if (((pulse == lastPulse) && (mode == lastMode)) || ((now - lastPrint) < STATUS_MS)) {
        return;
    }
    lastPulse = pulse;
    lastMode = mode;
    lastPrint = now;
    printf("mode=%-6s pulse=%4u us  pot=%4d\r\n", modeNames[mode], pulse, pot);
}

/*******************************************************************************
 * MAIN                                                                        *
 ******************************************************************************/
int main(void)
{
    char key;
    int16_t pot;

    BOARD_Init(); // also starts serial and the A/D (battery monitor)
    TIMERS_Init();
    AD_AddPins(POT_PIN);
    LED_Init();
    LED_AddBanks(LED_BANK1 | LED_BANK2 | LED_BANK3);
    SetLedBar(0);
    RC_Init();
    RC_AddPins(SERVO_PIN); // starts centered at 1500 us

    printf("\r\nECE118 Lab 3 Part 1 - RC servo on V4, pot on V3\r\n");
    PrintHelp();

    // The A/D adds V3 at its next interrupt and reorders its buffer, so wait for
    // a complete scan that includes V3 before trusting a reading.
    while (!(AD_ActivePins() & POT_PIN)) {
        ;
    }
    AD_IsNewDataReady(); // clears the flag left by the scan from before V3 was added
    while (!AD_IsNewDataReady()) {
        ;
    }

    while (1) {
        key = GetChar();
        if (key) {
            HandleKey(key);
        }

        pot = ReadPot();
        switch (mode) {
        case MODE_POT:
            if (pot >= 0) {
                SetPulse(MINPULSE + ((int32_t) pot * PULSE_RANGE + ADC_MAX / 2) / ADC_MAX);
            }
            break;
        case MODE_SWEEP:
            UpdateSweep();
            break;
        case MODE_MANUAL:
            break; // pulse only changes from the keyboard
        }

        // in pot mode this is the scaled pot reading; otherwise it tracks the servo
        SetLedBar(BarPattern(pulse - MINPULSE, PULSE_RANGE, NUM_LEDS));
        PrintStatus(pot);
    }
    return 0;
}
