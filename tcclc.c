/*
 * tcclc.c 3.1
 *
 * Control logic evaluated every 300 ms gate:
 *
 *   IF vehicle_speed > 30 mph:
 *       clutch_state = ENGAGED
 *   ELSE IF engine_speed < 1100 rpm:
 *       clutch_state = DISENGAGED
 *   ELSE IF vehicle_speed > 15 mph:
 *       clutch_state = ENGAGED
 *   ELSE IF vehicle_speed < 14 mph:
 *       clutch_state = DISENGAGED
 *   ELSE:
 *       clutch_state = clutch_state
 *
 * Ratios:
 *   ABS Frequency        2.2 Hz per mph
 *   Engine Frequency     1/5 Hz per rpm
 *
 * Gate time:
 *   300 ms
 */

#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdint.h>

/* -------- Pin assignments -------- */
#define ABS_PIN         PB4
#define ENGINE_PIN      PB2
#define CLUTCH_PIN      PB1

/* -------- Gate timing -------- */
#define T0_OCR0A_VALUE  124u
#define T0_TICK_MS      16u
#define GATE_MS_TARGET  300u

/* -------- Thresholds for 300 ms gate --------
 *
 * ABS Frequency = 2.2 Hz per mph
 *
 * 30 mph -> 66.0 Hz -> 19.8 counts / 300 ms -> engage if count >= 20
 * 15 mph -> 33.0 Hz ->  9.9 counts / 300 ms -> engage if count >= 10
 * 10 mph -> 22.0 Hz ->  6.6 counts / 300 ms -> disengage if count <= 6
 *
 * Engine Frequency = 1/5 Hz per rpm
 *
 * 1200 rpm -> 240 Hz
 * 240 Hz * 0.300 s = 72 pulses
 *
 * engine_speed < 1200 rpm -> count < 72 -> disengage if count <= 71
 */
#define ABS_FORCE_ENGAGE_COUNT   20u
#define ENGINE_MIN_COUNT         66u
#define ABS_ENGAGE_COUNT         10u
#define ABS_DISENGAGE_COUNT       9u

/* -------- Shared ISR state -------- */
static volatile uint16_t abs_count = 0;
static volatile uint16_t engine_count = 0;

static volatile uint8_t last_abs = 0;
static volatile uint8_t last_engine = 0;

static volatile uint16_t gate_ms = 0;

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
    uint8_t engine = (uint8_t)((pins >> ENGINE_PIN) & 1u);

    if ((last_abs == 0u) && (abs != 0u)) {
        abs_count++;
    }

    if ((last_engine == 0u) && (engine != 0u)) {
        engine_count++;
    }

    last_abs = abs;
    last_engine = engine;
}

/* -------- Timer0 gate logic -------- */
ISR(TIMER0_COMPA_vect)
{
    gate_ms = (uint16_t)(gate_ms + T0_TICK_MS);

    if (gate_ms < GATE_MS_TARGET) {
        return;
    }

    gate_ms = (uint16_t)(gate_ms - GATE_MS_TARGET);

    /* Snapshot and clear counts for this gate */
    uint16_t abs = abs_count;
    uint16_t engine = engine_count;

    abs_count = 0u;
    engine_count = 0u;

    /* Evaluate in exact requested order */

    /* IF vehicle_speed > 30 mph */
    if (abs >= ABS_FORCE_ENGAGE_COUNT) {
        PORTB |= (uint8_t)(1u << CLUTCH_PIN);
        clutch_state = CLUTCH_ENGAGED;
    }
    /* ELSE IF engine_speed < 1200 rpm */
    else if (engine < ENGINE_MIN_COUNT) {
        PORTB &= (uint8_t)~(1u << CLUTCH_PIN);
        clutch_state = CLUTCH_DISENGAGED;
    }
    /* ELSE IF vehicle_speed > 15 mph */
    else if (abs >= ABS_ENGAGE_COUNT) {
        PORTB |= (uint8_t)(1u << CLUTCH_PIN);
        clutch_state = CLUTCH_ENGAGED;
    }
    /* ELSE IF vehicle_speed < 10 mph */
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

    /* ABS and engine inputs */
    DDRB &= (uint8_t)~((1u << ABS_PIN) | (1u << ENGINE_PIN));

    last_abs = (uint8_t)((PINB >> ABS_PIN) & 1u);
    last_engine = (uint8_t)((PINB >> ENGINE_PIN) & 1u);

    /* Enable only the two PCINT sources in use */
    PCMSK |= (uint8_t)((1u << PCINT4) | (1u << PCINT2));
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
