/* Lab 3, Parts 5 and 6: serial stepper test console.
 * Set exactly one mode in Stepper.h, build, then use a 115200-baud terminal.
 * Commands: r <steps/s>, f <steps>, b <steps>, x, ?
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <xc.h>
#include <BOARD.h>
#include <serial.h>
#include <Stepper.h>

/* The ds30 Loader terminal sends a typed line without a newline, so a line
 * also runs once no characters have arrived for 250 ms (core timer = 40 MHz). */
#define LINE_IDLE_TICKS (40000000u / 4u)

static unsigned char ParseNumber(const char *text, unsigned long *value)
{
    char *end;
    while (isspace((unsigned char)*text)) {
        text++;
    }
    if (*text == '\0' || *text == '-') {
        return 0u;
    }
    *value = strtoul(text, &end, 10);
    if (end == text) {
        return 0u;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    return (*end == '\0') ? 1u : 0u;
}

static void Help(void)
{
    printf("\r\nLab 3 stepper console\r\n");
    printf("r N  set step rate N Hz (0 means 0.5 Hz, max 20000)\r\n");
    printf("f N  step forward N steps\r\n");
    printf("b N  step backward N steps\r\n");
    printf("x    stop and de-energize coils\r\n");
    printf("?    show rate and remaining steps\r\n");
}

static void Execute(char *line)
{
    unsigned long value;
    char command = line[0];

    if (command == '?') {
        printf("\r\nrate=%u, remaining=%ld, running=%d\r\n",
               Stepper_GetRate(), (long)Stepper_GetRemainingSteps(),
               Stepper_IsStepping());
        return;
    }
    if (command == 'x' || command == 'X') {
        Stepper_StopsSteps();
        printf("\r\nStopped; outputs disabled.\r\n");
        return;
    }
    if (command != 'r' && command != 'R' && command != 'f' &&
        command != 'F' && command != 'b' && command != 'B') {
        Help();
        return;
    }
    if (!ParseNumber(line + 1, &value)) {
        printf("\r\nEnter a nonnegative whole number after the command.\r\n");
        return;
    }
    if (command == 'r' || command == 'R') {
        if (value > 20000ul || Stepper_SetRate((uint16_t)value) != SUCCESS) {
            printf("\r\nRate must be 0..20000.\r\n");
        } else {
            printf("\r\nRate set to %u Hz.\r\n", Stepper_GetRate());
        }
        return;
    }
    if (value == 0ul || value > LONG_MAX) {
        printf("\r\nStep count must be 1..%ld.\r\n", (long)LONG_MAX);
        return;
    }
    if (Stepper_InitSteps((command == 'f' || command == 'F') ? FORWARD : REVERSE,
                          (int32_t)value) != SUCCESS) {
        printf("\r\nMotor is busy; use x or wait for the move to finish.\r\n");
    } else {
        printf("\r\nStarted %lu steps.\r\n", value);
    }
}

int main(void)
{
    char line[40];
    unsigned int length = 0u;
    unsigned char wasRunning = 0u;
    unsigned int lastCharTime = 0u;

    BOARD_Init();
    if (Stepper_Init() != SUCCESS) {
        printf("\r\nStepper initialization failed.\r\n");
        while (1) {
        }
    }
    Help();

    for (;;) {
        char ch = GetChar();
        unsigned char running = (unsigned char)Stepper_IsStepping();

        if (wasRunning && !running) {
            printf("\r\nMove finished. Use x to release holding current.\r\n");
        }
        wasRunning = running;
        if (ch == '\0') {
            if (length != 0u && (_CP0_GET_COUNT() - lastCharTime) > LINE_IDLE_TICKS) {
                line[length] = '\0';
                Execute(line);
                length = 0u;
            }
            continue;
        }
        lastCharTime = _CP0_GET_COUNT();
        if (ch == '\r' || ch == '\n') {
            if (length != 0u) {
                line[length] = '\0';
                Execute(line);
                length = 0u;
            }
        } else if (ch == '\b' || ch == 127) {
            if (length != 0u) {
                length--;
            }
        } else if (length < sizeof(line) - 1u) {
            line[length++] = ch;
        } else {
            length = 0u;
            printf("\r\nCommand too long.\r\n");
        }
    }
}
