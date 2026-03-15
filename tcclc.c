/*
 * tcclc.c 2.0
 * Changes:
 *  - Gate reduced from 1000 ms to 300 ms
 *  - Added clutch override cam speed input (default PB2)
 *  - Output forced LOW if override frequency < ~8.3 Hz or about 1200 rpm
 *  - Uses two PCINT mask bits (one per input)
 *  - Refactored edge detection to reduce duplicate code
 *
 * Toolchain:
 *  - ATtiny85
 *  - avr-gcc C99
 */

#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdint.h>

/* -------- Pin assignments -------- */
#define ABS_PIN       PB4
#define CAM_PIN    PB2
#define CLUTCH_PIN    PB1

/* -------- Gate timing -------- */
#define T0_OCR0A_VALUE   124u
#define T0_TICK_MS       16u
#define GATE_MS_TARGET   300u

/* -------- ABS Frequency thresholds (scaled for 300 ms gate) -------- */
#define THRESH_RISE_COUNT   10u
#define THRESH_FALL_COUNT   9u

/* Engine speed from cam: 8.3 Hz * 0.300 s ≈ 2.49 → require ≥2 pulses */
#define CAM_MIN_COUNT  2u /* force clutch to disengage if cam is too slow */

#define LOCKOUT_GATES       1u

/* -------- Shared ISR state -------- */

static volatile uint16_t abs_count = 0;
static volatile uint16_t cam_count = 0;

static volatile uint8_t last_abs = 0;
static volatile uint8_t last_cam = 0;

static volatile uint16_t gate_ms = 0;
static volatile uint8_t lockout = 0;

typedef enum
{
    CLUTCH_DISENGAGE = 0,
    CLUTCH_ENGAGE = 1
} out_state_t;

static volatile out_state_t state = CLUTCH_DISENGAGE;

/* -------- Pin-change interrupt -------- */

ISR(PCINT0_vect)
{
    uint8_t pins = PINB;

    uint8_t abs = (pins >> ABS_PIN) & 1;
    uint8_t cam = (pins >> CAM_PIN) & 1;

    if (!last_abs && abs)
        abs_count++;

    if (!last_cam && cam)
        cam_count++;

    last_abs = abs;
    last_cam = cam;
}

/* -------- Timer0 gate logic -------- */

ISR(TIMER0_COMPA_vect)
{
    gate_ms += T0_TICK_MS;

    if (gate_ms < GATE_MS_TARGET)
        return;

    gate_ms -= GATE_MS_TARGET;

    uint16_t abs = abs_count;
    uint16_t cam = cam_count;

    abs_count = 0;
    cam_count = 0;

    /* Override has priority over everything else. */
    if (cam < CAM_MIN_COUNT)
    {
        PORTB &= (uint8_t)~(1u << CLUTCH_PIN);
        state = CLUTCH_DISENGAGE;
        return;
    }

    if (lockout)
    {
        lockout--;
        return;
    }

    if (state == CLUTCH_DISENGAGE)
    {
        if (abs >= THRESH_RISE_COUNT)
        {
            PORTB |= (uint8_t)(1u << CLUTCH_PIN);
            state = CLUTCH_ENGAGE;
            lockout = LOCKOUT_GATES;
        }
    }
    else
    {
        if (abs <= THRESH_FALL_COUNT)
        {
            PORTB &= (uint8_t)~(1u << CLUTCH_PIN);
            state = CLUTCH_DISENGAGE;
            lockout = LOCKOUT_GATES;
        }
    }
}

/* -------- IO init -------- */

static void io_init(void)
{
    DDRB |= (1 << CLUTCH_PIN);
    PORTB &= ~(1 << CLUTCH_PIN);

    DDRB &= ~((1 << ABS_PIN) | (1 << CAM_PIN));

    last_abs = (PINB >> ABS_PIN) & 1;
    last_cam = (PINB >> CAM_PIN) & 1;

    /* enable PCINT on only the two pins we use */
    PCMSK |= (1 << PCINT4) | (1 << PCINT2);
    GIMSK |= (1 << PCIE);
}

/* -------- Timer0 init -------- */

static void timer0_init(void)
{
    TCCR0A = (1 << WGM01);
    OCR0A = T0_OCR0A_VALUE;

    TIMSK |= (1 << OCIE0A);

    TCCR0B = (1 << CS02) | (1 << CS00);
}

/* -------- main -------- */

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
