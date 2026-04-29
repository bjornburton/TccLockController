/*
 * tcclc.c
 * Version 1.0
 * 2026-03-01
 *
 * Requirements implemented:
 *  - ATtiny85, internal RC oscillator, F_CPU = 8 MHz (no trimming), which is good enough.
 *  - TTL input on PB4 (rising-edge counted).
 *  - TTL output on PB1, with red LED.
 *  - Output initializes LOW at power-up/reset.
 *  - Frequency decision from rising-edge count over ~1 s gate.
 *  - Frequency hysteresis:
 *      * LOW  -> HIGH when f > 57 Hz  (integer 1 s gate => count >= 57)
 *      * HIGH -> LOW  when f < 52 Hz  (integer 1 s gate => count <= 52)
 *  - Temporal lockout: inhibit output transitions for 1 s after any transition.
 *      * If frequency crosses thresholds during lockout, evaluate immediately after lockout expires
 *        (i.e., the first eligible 1 s gate after lockout ends).
 *
 * Toolchain: Microchip.ATtiny_DFP.3.3.272, avr-gcc (C99), avrdude, Atmel ICE
 */

#define F_CPU 8000000UL

# include <avr/io.h>
# include <avr/interrupt.h>
# include <avr/sleep.h>
# include <avr/wdt.h>
# include <stdint.h>

/* ---- Pin assignments (preferred) ---- */
#define IN_PIN      PB4
#define OUT_PIN     PB1

/* ---- Gate timing using Timer0 CTC ----
 * Prescaler = 1024
 * Timer tick = F_CPU / 1024 = 7812.5 Hz (nominal with perfect 8 MHz RC)
 * OCR0A = 124 -> compare period = (124+1)/7812.5 = 125/7812.5 = 0.016 s = 16 ms (nominal)
 * So ~62.5 interrupts per second. We accumulate 16 ms "quanta" to ~1000 ms.
 */
#define T0_OCR0A_VALUE      124u
#define T0_TICK_MS          16u
#define GATE_MS_TARGET      300u

/* ---- Thresholds expressed as counts per 1 s gate ----
 * Because decision is based on integer rising-edge count over ~1 s:
 *   f > 58 Hz  -> count >= 58
 *   f < 52 Hz  -> count <= 52
 */
#define THRESH_RISE_COUNT   18u
#define THRESH_FALL_COUNT   15u

/* ---- Temporal lockout in gate units ----
 * Minimum 1 s between output transitions.
 * Since decisions occur only at gate boundaries (~1 s), enforcing 1 gate of lockout
 * after a transition ensures >= 1 s between transitions.
 */
#define LOCKOUT_GATES       1u

/* Volatile state shared with ISRs */
static volatile uint16_t g_edge_count = 0;      /* rising-edge count within current gate */
static volatile uint8_t  g_last_in_level = 0;   /* last sampled IN level for rising detect */

static volatile uint16_t g_gate_ms_accum = 0;   /* accumulates 16 ms quanta */
static volatile uint8_t  g_lockout_gates = 0;   /* remaining gates inhibited */

/* Output state */
typedef enum {
    OUT_LOW = 0,
    OUT_HIGH = 1
} out_state_t;

static volatile out_state_t g_out_state = OUT_LOW;

/* ---- Pin-change interrupt: count rising edges on PB4 ---- */
ISR(PCINT0_vect)
{
    uint8_t pinb = PINB;
    uint8_t in_level = (pinb >> IN_PIN) & 0x01u;

    /* Rising edge: 0 -> 1 */
    if ((g_last_in_level == 0u) && (in_level != 0u)) {
        g_edge_count++;
    }

    g_last_in_level = in_level;
}

/* ---- Timer0 compare match: accumulate to ~1 s gate and run state update ---- */
ISR(TIMER0_COMPA_vect)
{
    g_gate_ms_accum = (uint16_t)(g_gate_ms_accum + T0_TICK_MS);

    if (g_gate_ms_accum >= GATE_MS_TARGET) {
        /* End of gate: consume 1000 ms and evaluate */
        g_gate_ms_accum = (uint16_t)(g_gate_ms_accum - GATE_MS_TARGET);

        /* Snapshot and reset edge count for next gate */
        uint16_t count = g_edge_count;
        g_edge_count = 0;

        /* Update lockout gating */
        if (g_lockout_gates != 0u) {
            g_lockout_gates--;
            return; /* transitions inhibited this gate */
        }

        /* Apply frequency hysteresis thresholds */
        if (g_out_state == OUT_LOW) {
            if (count >= THRESH_RISE_COUNT) {
                /* LOW -> HIGH */
                PORTB |= (uint8_t)(1u << OUT_PIN);
                g_out_state = OUT_HIGH;
                g_lockout_gates = LOCKOUT_GATES;
            }
        } else { /* OUT_HIGH */
            if (count <= THRESH_FALL_COUNT) {
                /* HIGH -> LOW */
                PORTB &= (uint8_t)~(1u << OUT_PIN);
                g_out_state = OUT_LOW;
                g_lockout_gates = LOCKOUT_GATES;
            }
        }
    }

    wdt_reset();
}

static void io_init(void)
{
    /* Output PB5: push-pull output, start LOW */
    DDRB  |= (uint8_t)(1u << OUT_PIN);
    PORTB &= (uint8_t)~(1u << OUT_PIN);
    g_out_state = OUT_LOW;

    /* Input PB4: input.
     * If you need an internal pull-up, uncomment the next line.
     * PORTB |= (uint8_t)(1u << IN_PIN);
     */
    DDRB  &= (uint8_t)~(1u << IN_PIN);

    /* Initialize last input level for clean rising-edge detection */
    g_last_in_level = (uint8_t)((PINB >> IN_PIN) & 0x01u);

    /* Enable pin-change interrupt on PB4 (PCINT4) */
    PCMSK |= (uint8_t)(1u << PCINT4);
    GIMSK |= (uint8_t)(1u << PCIE);
}

static void timer0_init(void)
{
    /* Timer0 CTC mode: WGM01 = 1, WGM00 = 0 */
    TCCR0A = (uint8_t)(1u << WGM01);
    TCCR0B = 0u;

    /* Compare value */
    OCR0A = (uint8_t)T0_OCR0A_VALUE;

    /* Enable compare match A interrupt */
    TIMSK |= (uint8_t)(1u << OCIE0A);

    /* Start timer with prescaler 1024: CS02=1, CS01=0, CS00=1 */
    TCCR0B = (uint8_t)((1u << CS02) | (1u << CS00));
}

int main(void)
{
    /* Ensure WDT is off before reconfiguring */
    wdt_disable();

    /* Enable watchdog reset, choose a timeout comfortably > 1 s gate */
    wdt_enable(WDTO_2S);
    wdt_reset();

    io_init();
    timer0_init();

    /* Sleep configuration: IDLE keeps Timer0 and pin-change interrupts running */
    set_sleep_mode(SLEEP_MODE_IDLE);

    sei();

    for (;;) {
        sleep_mode();
        /* All work is ISR-driven */
    }
}
