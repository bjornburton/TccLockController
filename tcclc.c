
/*
 * tcclc.c 3.3
 *
 * Fully commented version.
 *
 * PURPOSE:
 * --------
 * Control a torque converter clutch (TCC) using:
 *   - Vehicle speed (ABS signal)
 *   - Engine speed (RPM signal)
 *
 * The clutch engages/disengages based on frequency thresholds.
 *
 * KEY FEATURES:
 * -------------
 * - Frequency measurement via edge counting (300 ms gate)
 * - Deterministic interrupt-driven design
 * - Asymmetric timing:
 *      -> Immediate disengage
 *      -> Delayed re-engagement
 *
 * --------------------------------------------------------------------
 */

#define F_CPU 8000000UL   /* Internal oscillator at 8 MHz */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdint.h>

/* --------------------------------------------------------------------
 * PIN ASSIGNMENTS
 * --------------------------------------------------------------------
 *
 * PB4 : ABS signal input (vehicle speed)
 * PB2 : Engine RPM signal input
 * PB1 : Clutch output (drive signal / LED)
 */
#define ABS_PIN      PB4
#define ENGINE_PIN   PB2
#define CLUTCH_PIN   PB1


/* --------------------------------------------------------------------
 * TIMER / GATE CONFIGURATION
 * --------------------------------------------------------------------
 *
 * Timer0 is configured to generate an interrupt every ~16 ms.
 * We accumulate these ticks to form a 300 ms measurement window.
 */
#define T0_OCR0A_VALUE  124u     /* Compare match value */
#define T0_TICK_MS      16u      /* Approximate tick period */
#define GATE_MS_TARGET  300u     /* Measurement window */


/* --------------------------------------------------------------------
 * RE-ENGAGEMENT INHIBIT
 * --------------------------------------------------------------------
 *
 * After a forced disengagement (engine RPM too low),
 * clutch re-engagement is blocked for a defined time.
 *
 * Adjustable range: 1000–2500 ms recommended.
 */
#define REENGAGE_DELAY_MS 1500u

/* Convert milliseconds to number of 300 ms gates */
#define REENGAGE_DELAY_GATES \
    ((REENGAGE_DELAY_MS + GATE_MS_TARGET - 1u) / GATE_MS_TARGET)


/* --------------------------------------------------------------------
 * THRESHOLDS (derived from signal ratios)
 * --------------------------------------------------------------------
 *
 * ABS: 2.2 Hz per mph
 * Engine: 1/5 Hz per rpm
 *
 * Gate time: 300 ms
 *
 * ABS thresholds:
 *   27 mph → ~18 pulses
 *   15 mph → ~10 pulses
 *
 * Engine threshold:
 *   950 rpm → ~57 pulses
 */
#define ABS_FORCE_ENGAGE_COUNT 18u
#define ABS_ENGAGE_COUNT       10u
#define ENGINE_MIN_COUNT       57u


/* --------------------------------------------------------------------
 * GLOBAL STATE (shared between ISRs)
 * --------------------------------------------------------------------
 */

/* Edge counters (incremented in PCINT ISR) */
static volatile uint16_t abs_count = 0;
static volatile uint16_t engine_count = 0;

/* Previous logic levels (for rising-edge detection) */
static volatile uint8_t last_abs = 0;
static volatile uint8_t last_engine = 0;

/* Gate timer accumulator */
static volatile uint16_t gate_ms = 0;

/* Re-engagement inhibit counter (in gate units) */
static volatile uint8_t reengage_inhibit = 0;


/* Clutch state */
typedef enum
{
    CLUTCH_DISENGAGED = 0,
    CLUTCH_ENGAGED    = 1
} clutch_state_t;

static volatile clutch_state_t clutch_state = CLUTCH_DISENGAGED;


/* --------------------------------------------------------------------
 * PIN CHANGE INTERRUPT (frequency measurement)
 * --------------------------------------------------------------------
 *
 * Triggered on ANY change of PB0–PB5.
 *
 * We:
 *   1. Read current pin states
 *   2. Detect rising edges
 *   3. Increment counters
 *
 * Rising-edge detection prevents double-counting.
 */
ISR(PCINT0_vect)
{
    uint8_t pins = PINB;

    uint8_t abs = (pins >> ABS_PIN) & 1u;
    uint8_t eng = (pins >> ENGINE_PIN) & 1u;

    /* Rising edge detection: previous=0, current=1 */
    if (!last_abs && abs)
        abs_count++;

    if (!last_engine && eng)
        engine_count++;

    last_abs = abs;
    last_engine = eng;
}


/* --------------------------------------------------------------------
 * TIMER0 ISR (core control loop)
 * --------------------------------------------------------------------
 *
 * Runs every ~16 ms.
 *
 * Responsibilities:
 *   - Maintain gate timing
 *   - Snapshot counts every 300 ms
 *   - Execute control logic
 */
ISR(TIMER0_COMPA_vect)
{
    /* Accumulate elapsed time */
    gate_ms += T0_TICK_MS;

    /* Not yet at 300 ms → exit */
    if (gate_ms < GATE_MS_TARGET)
        return;

    /* One full gate reached */
    gate_ms -= GATE_MS_TARGET;

    /* Snapshot counts */
    uint16_t abs = abs_count;
    uint16_t eng = engine_count;

    /* Reset counters for next gate */
    abs_count = 0;
    engine_count = 0;

    /* Decrement inhibit timer if active */
    if (reengage_inhibit > 0)
        reengage_inhibit--;

    /* ----------------------------------------------------------------
     * CONTROL LOGIC (strict evaluation order)
     * ----------------------------------------------------------------
     */

    /* Rule 1: High speed → force engage */
    if (abs >= ABS_FORCE_ENGAGE_COUNT)
    {
        if (reengage_inhibit == 0)
        {
            PORTB |= (1 << CLUTCH_PIN);
            clutch_state = CLUTCH_ENGAGED;
        }
    }

    /* Rule 2: Low engine RPM → force disengage */
    else if (eng < ENGINE_MIN_COUNT)
    {
        PORTB &= ~(1 << CLUTCH_PIN);
        clutch_state = CLUTCH_DISENGAGED;

        /* Start re-engagement delay */
        reengage_inhibit = REENGAGE_DELAY_GATES;
    }

    /* Rule 3: Moderate speed → engage */
    else if (abs >= ABS_ENGAGE_COUNT)
    {
        if (reengage_inhibit == 0)
        {
            PORTB |= (1 << CLUTCH_PIN);
            clutch_state = CLUTCH_ENGAGED;
        }
    }

    /* Rule 4: Otherwise hold state */
    else
    {
        /* No change */
    }
}


/* --------------------------------------------------------------------
 * IO INITIALIZATION
 * --------------------------------------------------------------------
 */
static void io_init(void)
{
    /* Configure output pin */
    DDRB |= (1 << CLUTCH_PIN);

    /* Ensure clutch is OFF at startup */
    PORTB &= ~(1 << CLUTCH_PIN);
    clutch_state = CLUTCH_DISENGAGED;

    /* Configure inputs */
    DDRB &= ~((1 << ABS_PIN) | (1 << ENGINE_PIN));

    /* Capture initial pin states */
    last_abs = (PINB >> ABS_PIN) & 1u;
    last_engine = (PINB >> ENGINE_PIN) & 1u;

    /* Enable pin-change interrupts ONLY for used pins */
    PCMSK |= (1 << PCINT4) | (1 << PCINT2);
    GIMSK |= (1 << PCIE);
}


/* --------------------------------------------------------------------
 * TIMER0 INITIALIZATION
 * --------------------------------------------------------------------
 */
static void timer0_init(void)
{
    /* CTC mode */
    TCCR0A = (1 << WGM01);

    /* Compare value */
    OCR0A = T0_OCR0A_VALUE;

    /* Enable interrupt */
    TIMSK |= (1 << OCIE0A);

    /* Start timer (prescaler = 1024) */
    TCCR0B = (1 << CS02) | (1 << CS00);
}


/* --------------------------------------------------------------------
 * MAIN
 * --------------------------------------------------------------------
 *
 * The CPU spends almost all time sleeping.
 * All work happens in interrupts.
 */
int main(void)
{
    io_init();
    timer0_init();

    set_sleep_mode(SLEEP_MODE_IDLE);
    sei();

    while (1)
    {
        sleep_mode();
    }
}
