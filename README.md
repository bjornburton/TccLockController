# TCC Lock Controller (ATtiny85)

## Overview

This firmware implements a frequency-based torque converter clutch (TCC)
lock controller using an ATtiny85 microcontroller.

The system:

-   Measures a square-wave speed signal.
-   Computes frequency using a 1-second gate measurement.
-   Engages the torque converter clutch above a defined frequency
    threshold.
-   Disengages the clutch below a lower threshold.
-   Enforces a 1-second minimum dwell time between state transitions.
-   Initializes in a safe (clutch OFF) state at power-up.
-   Brownout detection is set to about 4.3 Volts. 
-   Watchdog set to about 2 seconds.

The system has been successfully implemented in a Jeep Grand Cherokee WJ
equipped with a 42RE 4-speed automatic transmission. The ABS speed
signal was chosen due to the non-digital nature of the transmission
output speed sensor signal (sine wave).

The firmware is fully interrupt-driven and spends most of its time in
sleep mode.

------------------------------------------------------------------------

## Hardware Platform

### Microcontroller

-   Device: ATtiny85
-   Clock: Internal 8 MHz RC oscillator
    -   `F_CPU = 8000000UL`
-   Recommended fuse configuration:
    -   `LFUSE = 0xE2` (CKDIV8 disabled, full 8 MHz)
    -   `HFUSE = 0xDF`
    -   `EFUSE = 0xFC` (Brown Out Detection of about 4.3 Volts)

### Board

Designed and tested on: - Adafruit Trinket 5 V (ATtiny85)

### Pin Mapping
| Function | ATtiny85 Pin | Description                                   |
|------------------------|---------------|--------------------------------|
| Input                  | PB4             | ABS speed signal (TTL)       |
| Output                 | PB1             | TCC driver output + LED      |
------------------------------------------------------------------------

> Note: PB1 is MISO during ISP programming. Ensure external circuitry
> does not drive this line during programming.


#### Atmel-ICE to Trinket Programming Connections

| Atmel-ICE AVR Port Pin | Mini-Squid Pin | Trinket Pin Assignment        |
|------------------------|---------------|--------------------------------|
| Pin 1 (TCK)            | 1             | CN4-2 / #2 (SCK)               |
| Pin 2 (GND)            | 2             | CN3-4 / Gnd (GND)              |
| Pin 3 (TDO)            | 3             | CN4-3 / #1 (MISO)              |
| Pin 4 (VTG)            | 4             | CN4-1 / 5V (VTG)               |
| Pin 6 (nSRST)          | 6             | CN3-1 / Rst (/RESET)           |
| Pin 9 (TDI)            | 9             | CN4-4 / #0 (MOSI)              |
------------------------------------------------------------------------

## Frequency Measurement Method

The firmware counts rising edges on PB4 over a 1-second gate interval.

Given (For Jeep WJ ABS speed signal): - Speed signal ≈ 2.2 Hz per mph

Typical thresholds:

-   Engage clutch at ≥ 57 Hz (\~26 mph)
-   Disengage clutch at ≤ 52 Hz (\~24 mph)

This produces approximately 2 mph hysteresis.

------------------------------------------------------------------------

## Timer Configuration

Timer0 is configured in CTC mode:

-   Prescaler: 1024
-   OCR0A = 124
-   Interrupt interval ≈ 16 ms

62.5 interrupts accumulate to approximately 1 second.

At each completed 1-second gate:

-   The edge count is latched.
-   The counter resets.
-   The state machine evaluates thresholds.

------------------------------------------------------------------------

## Control Logic

### Frequency Hysteresis

-   LOW → HIGH if count ≥ 57
-   HIGH → LOW if count ≤ 52

### Temporal Lockout

After any transition:

-   Output is locked for 1 full gate (\~1 s).
-   If frequency crosses during lockout, evaluation occurs immediately
    after lockout expires.

------------------------------------------------------------------------

## State Machine

At each 1-second gate:

If lockout active: - Decrement lockout counter. - No state change.

If lockout inactive: - If LOW and count ≥ 57 → set HIGH and start
lockout. - If HIGH and count ≤ 52 → set LOW and start lockout. -
Otherwise retain state.

------------------------------------------------------------------------

## Power Behavior

-   Output initializes LOW at power-up.
-   Clutch is disengaged until first valid decision cycle.

------------------------------------------------------------------------

## Execution Model

The firmware is interrupt-driven:

-   Pin-change ISR counts rising edges.
-   Timer0 ISR handles gate timing and state evaluation.
-   Main loop remains in `sleep_mode()` (IDLE).

------------------------------------------------------------------------

## Build

Example build flags:

``` bash
avr-gcc -mmcu=attiny85 -std=c99 -DF_CPU=8000000UL -Os -o tcclc.elf tcclc.c
avr-objcopy -O ihex tcclc.elf tcclc.hex
```

------------------------------------------------------------------------

## Programming (Atmel-ICE)

``` bash
avrdude -p t85 -c atmelice_isp -P usb -U flash:w:tcclc.hex:i
```

Set fuses:

``` bash
avrdude -p t85 -c atmelice_isp -P usb -U lfuse:w:0xE2:m -U hfuse:w:0xDF:m -U efuse:w:0xFC:m
```

------------------------------------------------------------------------

## Design Characteristics

### Advantages
-   Deterministic timing
-   Integer arithmetic only
-   Low CPU load
-   Robust frequency and temporal hysteresis
-   Safe startup behavior

### Limitations

-   Frequency resolution limited to 1 Hz (1 s gate)
-   Up to 1 s decision latency
-   Threshold drift with internal RC oscillator tolerance

------------------------------------------------------------------------

## Summary

This project implements a simple, robust, and low-power frequency-based
torque converter clutch controller suitable for automotive applications
where vehicle speed is derived from a digital speed signal.
