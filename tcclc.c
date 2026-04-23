\
/*
 * tcclc_abs_only_mph.c
 *
 * ATtiny85, 8 MHz internal oscillator
 *
 * Control logic evaluated every 300 ms gate:
 *
 *   IF vehicle_speed > 15 mph:
 *       clutch_state = ENGAGED
 *   ELSE IF vehicle_speed < 9 mph:
 *       clutch_state = DISENGAGED
 *   ELSE:
 *       clutch_state = clutch_state
 *
 * Ratios:
 *   ABS Frequency  2.2 Hz per mph
 *
 * Gate time:
 *   300 ms
 *
 * Thresholds are entered as integer mph values and converted at compile time
 * to integer counts per gate using the C preprocessor.
 */

#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdint.h>

/* -------- Pin assignments -------- */
#define ABS_PIN         PB4
#define CLUTCH_PIN      PB1

/* -------- Gate timing -------- */
#define T0_OCR0A_VALUE  124u
#define T0_TICK_MS      16u
#define GATE_MS_TARGET  300u

/* -------- User thresholds (integer mph) -------- */
#define ENGAGE_MPH       9u
#define DISENGAGE_MPH    7u

/* -------- Signal scaling --------
 *
 * ABS Frequency = 2.2 Hz per mph = 22/10 Hz per mph
 *
 * counts per gate = mph * (22/10) * (GATE_MS_TARGET/1000)
 *
 * To keep the thresholds conservative and consistent with the control logic:
 *
 *   vehicle_speed > ENGAGE_MPH
 *     => engage when count >= ceil( mph * 22 * gate_ms / 10000 )
 *
 *   vehicle_speed < DISENGAGE_MPH
 *     => disengage when count <= floor( mph * 22 * gate_ms / 10000 ) - 1
 *
 * For GATE_MS_TARGET = 300 ms:
 *   15 mph -> 9.9 counts -> engage at >= 10
 *    9 mph -> 5.94 counts -> disengage at <= 5
 */
#define ABS_HZ_PER_MPH_NUM   22u
#define ABS_HZ_PER_MPH_DEN   10u

#define MPH_TO_GATE_COUNTS_NUM(mph) \
    ((uint32_t)(mph) * ABS_HZ_PER_MPH_NUM * (uint32_t)GATE_MS_TARGET)

#define MPH_TO_GATE_COUNTS_CEIL(mph) \
    ((uint16_t)((MPH_TO_GATE_COUNTS_NUM(mph) + 9999u) / 10000u))

#define MPH_TO_GATE_COUNTS_FLOOR(mph) \
    ((uint16_t)(MPH_TO_GATE_COUNTS_NUM(mph) / 10000u))

#define ABS_ENGAGE_COUNT     MPH_TO_GATE_COUNTS_CEIL(ENGAGE_MPH)
#define ABS_DISENGAGE_COUNT  ((uint16_t)(MPH_TO_GATE_COUNTS_FLOOR(DISENGAGE_MPH) - 1u))

/* -------- Shared ISR state -------- */
static volatile uint16_t abs_count = 0u;
static volatile uint8_t last_abs = 0u;
static volatile uint16_t gate_ms = 0u;

typedef enum
{
    CLUTCH_DISENGAGED = 0,
    CLUTCH_ENGAGED    = 1
} clutch_state_t;

static volatile clutch_state_t clutch_state = CLUTCH_DISENGAGED;

/* -------- Pin-change interrupt -------- */
ISR(PCINT0_vect)
{
    uint8_t pins = PINB;
    uint8_t abs = (uint8_t)((pins >> ABS_PIN) & 1u);

    if ((last_abs == 0u) && (abs != 0u)) {
        abs_count++;
    }

    last_abs = abs;
}

/* -------- Timer0 gate logic -------- */
ISR(TIMER0_COMPA_vect)
{
    gate_ms = (uint16_t)(gate_ms + T0_TICK_MS);

    if (gate_ms < GATE_MS_TARGET) {
        return;
    }

    gate_ms = (uint16_t)(gate_ms - GATE_MS_TARGET);

    /* Snapshot and clear count for this gate */
    uint16_t abs = abs_count;
    abs_count = 0u;

    /* Evaluate control logic */

    /* IF vehicle_speed > ENGAGE_MPH */
    if (abs >= ABS_ENGAGE_COUNT) {
        PORTB |= (uint8_t)(1u << CLUTCH_PIN);
        clutch_state = CLUTCH_ENGAGED;
    }
    /* ELSE IF vehicle_speed < DISENGAGE_MPH */
    else if (abs <= ABS_DISENGAGE_COUNT) {
        PORTB &= (uint8_t)~(1u << CLUTCH_PIN);
        clutch_state = CLUTCH_DISENGAGED;
    }
    /* ELSE maintain previous state */
    else {
        /* no change */
    }
}

static void io_init(void)
{
    /* Clutch output low at startup */
    DDRB  |= (uint8_t)(1u << CLUTCH_PIN);
    PORTB &= (uint8_t)~(1u << CLUTCH_PIN);
    clutch_state = CLUTCH_DISENGAGED;

    /* ABS input */
    DDRB &= (uint8_t)~(1u << ABS_PIN);

    last_abs = (uint8_t)((PINB >> ABS_PIN) & 1u);

    /* Enable only the ABS PCINT source */
    PCMSK |= (uint8_t)(1u << PCINT4);
    GIMSK |= (uint8_t)(1u << PCIE);
}

static void timer0_init(void)
{
    /* Timer0 CTC mode */
    TCCR0A = (uint8_t)(1u << WGM01);
    TCCR0B = 0u;

    OCR0A = (uint8_t)T0_OCR0A_VALUE;

    TIMSK |= (uint8_t)(1u << OCIE0A);

    /* Prescaler = 1024 */
    TCCR0B = (uint8_t)((1u << CS02) | (1u << CS00));
}

int main(void)
{
    io_init();
    timer0_init();

    set_sleep_mode(SLEEP_MODE_IDLE);
    sei();

    for (;;) {
        sleep_mode();
    }
}
