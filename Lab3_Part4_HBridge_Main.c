/* Lab 3, Part 4: bidirectional DC motor on L298 bridge A.
 * Potentiometer: PORT W pin 4, 0..3.3 V.
 * L298 CON2: ENA <- PORT Z pin 6, IN1 <- Z3, IN2 <- Z5.
 * Direction switch: PORT Y pin 8 with external pull-up to 3.3 V;
 * closing the switch to ground requests reverse rotation.
 */

#include <xc.h>
#include <BOARD.h>
#include <AD.h>
#include <LED.h>
#include <PWM.h>

#define POT_PIN AD_PORTW4
#define ENABLE_PWM_PIN PWM_PORTZ06
#define ADC_MAX 1023u

#define DIR_A_TRIS TRISEbits.TRISE4 /* Z3 */
#define DIR_A_OUT LATEbits.LATE4
#define DIR_B_TRIS TRISEbits.TRISE3 /* Z5 */
#define DIR_B_OUT LATEbits.LATE3
#define SWITCH_TRIS TRISDbits.TRISD9 /* Y8 */
#define SWITCH_IN PORTDbits.RD9

static void ShowSpeedAndDirection(unsigned int reading, unsigned char reverse)
{
    unsigned int lit = (reading * 12u + ADC_MAX / 2u) / ADC_MAX;
    unsigned int pattern = (1u << lit) - 1u;

    if (reverse) {
        unsigned int flipped = 0u;
        unsigned int bit;
        for (bit = 0u; bit < 12u; bit++) {
            flipped |= ((pattern >> bit) & 1u) << (11u - bit);
        }
        pattern = flipped;
    }
    LED_SetBank(LED_BANK1, (unsigned char)(pattern & 0x0fu));
    LED_SetBank(LED_BANK2, (unsigned char)((pattern >> 4) & 0x0fu));
    LED_SetBank(LED_BANK3, (unsigned char)((pattern >> 8) & 0x0fu));
}

int main(void)
{
    unsigned char reverse = 0u;

    BOARD_Init();
    DIR_A_OUT = 0;
    DIR_B_OUT = 0;
    DIR_A_TRIS = 0;
    DIR_B_TRIS = 0;
    SWITCH_TRIS = 1;

    if (AD_AddPins(POT_PIN) != SUCCESS || LED_Init() != SUCCESS ||
        LED_AddBanks(LED_BANK1 | LED_BANK2 | LED_BANK3) != SUCCESS ||
        PWM_Init() != SUCCESS || PWM_AddPins(ENABLE_PWM_PIN) != SUCCESS ||
        PWM_SetFrequency(PWM_1KHZ) != SUCCESS ||
        PWM_SetDutyCycle(ENABLE_PWM_PIN, MIN_PWM) != SUCCESS) {
        while (1) {
        }
    }
    while ((AD_ActivePins() & POT_PIN) == 0u) {
    }

    for (;;) {
        unsigned int reading;
        unsigned int duty;
        unsigned char requestedReverse;

        if (!AD_IsNewDataReady()) {
            continue;
        }
        reading = AD_ReadADPin(POT_PIN);
        if (reading > ADC_MAX) {
            continue;
        }
        requestedReverse = (SWITCH_IN == 0) ? 1u : 0u;
        duty = (reading * MAX_PWM + ADC_MAX / 2u) / ADC_MAX;

        if (requestedReverse != reverse) {
            PWM_SetDutyCycle(ENABLE_PWM_PIN, MIN_PWM);
            DIR_A_OUT = requestedReverse ? 0 : 1;
            DIR_B_OUT = requestedReverse ? 1 : 0;
            reverse = requestedReverse;
        } else if (DIR_A_OUT == DIR_B_OUT) {
            /* Establish a direction before first enabling the bridge. */
            DIR_A_OUT = reverse ? 0 : 1;
            DIR_B_OUT = reverse ? 1 : 0;
        }
        PWM_SetDutyCycle(ENABLE_PWM_PIN, duty);
        ShowSpeedAndDirection(reading, reverse);
    }
}
