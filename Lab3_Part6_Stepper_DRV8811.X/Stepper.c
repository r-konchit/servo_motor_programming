/* Lab 3 stepper module. Uses Timer 3 and the supplied Stepper.h API.
 * Full, wave, half, and DRV8811 modes are selected in Stepper.h.
 */

#include <Stepper.h>
#include <xc.h>
#include <sys/attribs.h>

#define PB_CLOCK_HZ 40000000ul
#define MAX_STEP_RATE 20000u
#define DEFAULT_STEP_RATE 100u

#ifdef DRV8811_DRIVE
/* J1 pin 5 goes to the DRV8811 nENABLE pin, which is active LOW:
 * 0 = outputs on (coils driven), 1 = outputs off. */
#define ENABLE_ON 0
#define ENABLE_OFF 1
#define STEP_TRIS TRISFbits.TRISF1 /* PORT Z pin 4 -> J1 STEP */
#define STEP_OUT LATFbits.LATF1
#define DIR_TRIS TRISEbits.TRISE4 /* PORT Z pin 3 -> J1 DIR */
#define DIR_OUT LATEbits.LATE4
#define ENABLE_TRIS TRISEbits.TRISE3 /* PORT Z pin 5 -> J1 ENABLE */
#define ENABLE_OUT LATEbits.LATE3
#else
#define A_ENABLE_TRIS TRISFbits.TRISF1 /* Z4 -> ENA */
#define A_ENABLE LATFbits.LATF1
#define A_IN1_TRIS TRISEbits.TRISE4 /* Z3 -> IN1 */
#define A_IN1 LATEbits.LATE4
#define A_IN2_TRIS TRISEbits.TRISE3 /* Z5 -> IN2 */
#define A_IN2 LATEbits.LATE3
#define B_ENABLE_TRIS TRISDbits.TRISD8 /* Z8 -> ENB */
#define B_ENABLE LATDbits.LATD8
#define B_IN3_TRIS TRISEbits.TRISE2 /* Z7 -> IN3 */
#define B_IN3 LATEbits.LATE2
#define B_IN4_TRIS TRISEbits.TRISE1 /* Z9 -> IN4 */
#define B_IN4 LATEbits.LATE1

#if defined(FULL_STEP_DRIVE)
static const int8_t phases[4][2] = {
    { 1,  1}, { 1, -1}, {-1, -1}, {-1,  1}
};
#define PHASE_COUNT 4u
#elif defined(WAVE_DRIVE)
static const int8_t phases[4][2] = {
    { 1,  0}, { 0, -1}, {-1,  0}, { 0,  1}
};
#define PHASE_COUNT 4u
#else /* HALF_STEP_DRIVE */
static const int8_t phases[8][2] = {
    { 1,  0}, { 1, -1}, { 0, -1}, {-1, -1},
    {-1,  0}, {-1,  1}, { 0,  1}, { 1,  1}
};
#define PHASE_COUNT 8u
#endif
#endif

enum StepperState { STEP_OFF, STEP_READY, STEP_RUNNING, STEP_HALTED };
static volatile enum StepperState state = STEP_OFF;
static volatile int32_t remaining = 0;
static volatile uint8_t direction = FORWARD;
static volatile uint16_t rateHz = DEFAULT_STEP_RATE;
static volatile uint16_t lowRateNumerator = 0u;
static volatile uint16_t lowRateDenominator = 1000u;
static volatile uint16_t lowRateAccumulator = 0u;
#ifndef DRV8811_DRIVE
static volatile uint8_t phaseIndex = 0u;
static volatile uint8_t phaseValid = 0u;
#endif

static void DisableDrive(void)
{
#ifdef DRV8811_DRIVE
    ENABLE_OUT = ENABLE_OFF;
    STEP_OUT = 0;
#else
    A_ENABLE = 0;
    B_ENABLE = 0;
#endif
}

#ifndef DRV8811_DRIVE
static void ApplyCoils(int8_t a, int8_t b)
{
    /* ENA/ENB low makes an unpowered coil coast. Opposite input levels
     * command current in the desired direction through each coil. */
    A_ENABLE = 0;
    B_ENABLE = 0;
    A_IN1 = (a > 0) ? 1 : 0;
    A_IN2 = (a < 0) ? 1 : 0;
    B_IN3 = (b > 0) ? 1 : 0;
    B_IN4 = (b < 0) ? 1 : 0;
    A_ENABLE = (a != 0) ? 1 : 0;
    B_ENABLE = (b != 0) ? 1 : 0;
}
#endif

static void PerformStep(void)
{
#ifdef DRV8811_DRIVE
    volatile unsigned int hold;
    DIR_OUT = (direction == FORWARD) ? 1 : 0;
    for (hold = 0u; hold < 12u; hold++) {
        __asm__ volatile("nop");
    }
    STEP_OUT = 1;
    for (hold = 0u; hold < 80u; hold++) {
        __asm__ volatile("nop");
    }
    STEP_OUT = 0;
#else
    if (!phaseValid) {
        phaseIndex = (direction == FORWARD) ? 0u : (PHASE_COUNT - 1u);
        phaseValid = 1u;
    } else if (direction == FORWARD) {
        phaseIndex = (uint8_t)((phaseIndex + 1u) % PHASE_COUNT);
    } else {
        phaseIndex = (uint8_t)((phaseIndex + PHASE_COUNT - 1u) % PHASE_COUNT);
    }
    ApplyCoils(phases[phaseIndex][0], phases[phaseIndex][1]);
#endif
    --remaining;
    if (remaining == 0) {
        state = STEP_HALTED;
        /* Hold the final position until Stepper_StopsSteps or Stepper_End.
         * Disconnect drive after the measurement to avoid prolonged heating. */
    }
}

int8_t Stepper_Init(void)
{
    if (state != STEP_OFF) {
        return ERROR;
    }
#ifdef DRV8811_DRIVE
    STEP_OUT = 0;
    ENABLE_OUT = ENABLE_OFF;
    DIR_OUT = 0;
    STEP_TRIS = 0;
    ENABLE_TRIS = 0;
    DIR_TRIS = 0;
#else
    A_ENABLE = 0;
    B_ENABLE = 0;
    A_IN1 = A_IN2 = B_IN3 = B_IN4 = 0;
    A_ENABLE_TRIS = B_ENABLE_TRIS = 0;
    A_IN1_TRIS = A_IN2_TRIS = B_IN3_TRIS = B_IN4_TRIS = 0;
    phaseIndex = 0u;
    phaseValid = 0u;
#endif
    remaining = 0;
    direction = FORWARD;
    state = STEP_READY;
    T3CON = 0;
    IFS0bits.T3IF = 0;
    IPC3bits.T3IP = 3;
    IPC3bits.T3IS = 0;
    IEC0bits.T3IE = 1;
    return Stepper_SetRate(DEFAULT_STEP_RATE);
}

int8_t Stepper_SetRate(uint16_t rate)
{
    uint32_t period;
    uint8_t prescale;

    if (state == STEP_OFF || rate > MAX_STEP_RATE) {
        return ERROR;
    }
    IEC0bits.T3IE = 0;
    T3CONbits.ON = 0;
    T3CON = 0;
    TMR3 = 0;
    lowRateAccumulator = 0u;

    if (rate < 10u) {
        /* A 1 kHz interrupt with fractional accumulation gives 1..9 Hz.
         * Zero is the supplied module's special 0.5 steps/s setting. */
        prescale = 3u; /* 1:8 */
        period = 5000u;
        lowRateNumerator = (rate == 0u) ? 1u : rate;
        lowRateDenominator = (rate == 0u) ? 2000u : 1000u;
    } else if (rate <= 76u) {
        prescale = 6u; /* 1:64 */
        period = PB_CLOCK_HZ / (64ul * rate);
        lowRateNumerator = 0u;
    } else {
        prescale = 3u; /* 1:8 */
        period = PB_CLOCK_HZ / (8ul * rate);
        lowRateNumerator = 0u;
    }
    T3CONbits.TCKPS = prescale;
    PR3 = (uint16_t)(period - 1u);
    rateHz = rate;
    IFS0bits.T3IF = 0;
    IEC0bits.T3IE = 1;
    T3CONbits.ON = 1;
    return SUCCESS;
}

uint16_t Stepper_GetRate(void) { return rateHz; }

int8_t Stepper_SetSteps(uint8_t stepDirection, int32_t steps)
{
    if (state == STEP_OFF || steps <= 0 ||
        (stepDirection != FORWARD && stepDirection != REVERSE)) {
        return ERROR;
    }
    IEC0bits.T3IE = 0;
    direction = stepDirection;
    remaining = steps;
    IEC0bits.T3IE = 1;
    return SUCCESS;
}

int8_t Stepper_StartSteps(void)
{
    if (state == STEP_OFF || state == STEP_RUNNING || remaining <= 0) {
        return ERROR;
    }
#ifdef DRV8811_DRIVE
    DIR_OUT = (direction == FORWARD) ? 1 : 0;
    STEP_OUT = 0;
    ENABLE_OUT = ENABLE_ON;
#endif
    state = STEP_RUNNING;
    return SUCCESS;
}

int8_t Stepper_InitSteps(uint8_t stepDirection, int32_t steps)
{
    if (state == STEP_RUNNING ||
        Stepper_SetSteps(stepDirection, steps) != SUCCESS) {
        return ERROR;
    }
    return Stepper_StartSteps();
}

int8_t Stepper_StopsSteps(void)
{
    if (state == STEP_OFF) {
        return ERROR;
    }
    state = STEP_HALTED;
    DisableDrive();
    return SUCCESS;
}

int32_t Stepper_GetRemainingSteps(void) { return remaining; }
int8_t Stepper_GetDirection(void) { return (int8_t)direction; }
int8_t Stepper_IsStepping(void) { return (state == STEP_RUNNING) ? 1 : 0; }

int8_t Stepper_End(void)
{
    if (state == STEP_OFF) {
        return ERROR;
    }
    IEC0bits.T3IE = 0;
    T3CONbits.ON = 0;
    DisableDrive();
#ifdef DRV8811_DRIVE
    STEP_TRIS = DIR_TRIS = ENABLE_TRIS = 1;
#else
    A_ENABLE_TRIS = A_IN1_TRIS = A_IN2_TRIS = 1;
    B_ENABLE_TRIS = B_IN3_TRIS = B_IN4_TRIS = 1;
#endif
    remaining = 0;
    state = STEP_OFF;
    return SUCCESS;
}

void __ISR(_TIMER_3_VECTOR, IPL3SOFT) Timer3IntHandler(void)
{
    IFS0bits.T3IF = 0;
    if (state != STEP_RUNNING) {
        return;
    }
    if (lowRateNumerator != 0u) {
        lowRateAccumulator = (uint16_t)(lowRateAccumulator + lowRateNumerator);
        if (lowRateAccumulator < lowRateDenominator) {
            return;
        }
        lowRateAccumulator = (uint16_t)(lowRateAccumulator - lowRateDenominator);
    }
    PerformStep();
}
