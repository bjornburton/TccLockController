
# TCC Lock Controller (ATtiny85)

## Overview

This firmware implements a torque‑converter clutch (TCC) lock controller
using an ATtiny85 microcontroller.

The controller:

- Measures **vehicle speed** from an ABS square‑wave signal.
- Measures **engine speed** from a digital RPM signal.
- Computes signal frequency using a **300 ms gate interval**.
- Evaluates clutch engagement logic once per gate.
- Initializes in a safe **clutch disengaged** state at power‑up.
- Runs almost entirely interrupt‑driven while the CPU remains in sleep mode.

The system has been tested in a **Jeep Grand Cherokee WJ with a 42RE
automatic transmission**. The ABS signal is used because the transmission
output speed sensor provides an analog sine signal rather than a digital
pulse stream.

---

## Hardware Platform

### Microcontroller

- Device: **ATtiny85**
- Clock: **Internal 8 MHz RC oscillator**
- `F_CPU = 8000000UL`

Recommended fuse configuration:

| Fuse | Value | Purpose |
|-----|------|---------|
| LFUSE | `0xE2` | CKDIV8 disabled (full 8 MHz clock) |
| HFUSE | `0xDF` | Standard configuration |
| EFUSE | `0xFC` | Brown‑out detection ≈ 4.3 V |

---

### Board

Tested on:

- **Adafruit Trinket 5V (ATtiny85)**

---

## Pin Mapping

| Function | ATtiny85 Pin | Description |
|---------|--------------|-------------|
| Vehicle Speed Input | PB4 | ABS speed signal (TTL) |
| Engine Speed Input | PB2 | Engine RPM signal (TTL) |
| Clutch Output | PB1 | TCC control output + LED |

---

#### Atmel‑ICE to Trinket Programming Connections

| Atmel‑ICE AVR Port Pin | Mini‑Squid Pin | Trinket Pin Assignment |
|-----------------------|---------------|-----------------------|
| Pin 1 (TCK) | 1 | CN4‑2 / #2 (SCK) |
| Pin 2 (GND) | 2 | CN3‑4 / GND |
| Pin 3 (TDO) | 3 | CN4‑3 / #1 (MISO) |
| Pin 4 (VTG) | 4 | CN4‑1 / 5V (VTG) |
| Pin 6 (nSRST) | 6 | CN3‑1 / RESET |
| Pin 9 (TDI) | 9 | CN4‑4 / #0 (MOSI) |

---

## Signal Ratios

The firmware converts frequency measurements to vehicle speed and engine RPM
implicitly using the following ratios:

| Signal | Ratio |
|------|------|
| ABS Frequency | **2.2 Hz per mph** |
| Engine Frequency | **1/5 Hz per rpm** |

Example conversions:

| Quantity | Frequency |
|---------|-----------|
| 27 mph | 59.4 Hz |
| 15 mph | 33.0 Hz |
| 950 rpm | 190 Hz |

---

## Frequency Measurement

Rising edges on the input pins are counted during a **300 ms gate interval**.

At the end of each gate:

1. Edge counts are latched.
2. Counters are reset.
3. Control logic evaluates clutch state.

This approach provides stable integer‑based frequency measurement with very
low CPU overhead.

---

## Threshold Conversion (300 ms Gate)

Using the signal ratios above:

| Condition | Frequency | Pulses in 300 ms | Firmware Constant |
|----------|-----------|------------------|------------------|
| 27 mph | 59.4 Hz | ≈17.8 | `ABS_FORCE_ENGAGE_COUNT = 18` |
| 15 mph | 33 Hz | ≈9.9 | `ABS_ENGAGE_COUNT = 10` |
| 950 rpm | 190 Hz | 57 | `ENGINE_MIN_COUNT = 57` |

These values correspond directly to the constants defined in the firmware.

---

## Control Logic

The clutch state is evaluated **once every 300 ms gate** using the following
rule order:

```
IF vehicle_speed > 27 mph:
    clutch_state = ENGAGED

ELSE IF engine_speed < 950 rpm:
    clutch_state = DISENGAGED

ELSE IF vehicle_speed > 15 mph:
    clutch_state = ENGAGED

ELSE:
    clutch_state = clutch_state
```

This ordering ensures:

- High‑speed lockup always engages the clutch.
- Engine stall protection disengages the clutch.
- Normal engagement occurs above 15 mph.
- Otherwise the clutch state remains unchanged.

---

## Timer Configuration

Timer0 operates in **CTC (Clear‑Timer‑on‑Compare) mode**.

| Parameter | Value |
|----------|------|
| Prescaler | 1024 |
| OCR0A | 124 |
| Interrupt period | ≈16 ms |

The firmware accumulates these interrupts until **300 ms** has elapsed,
which defines the measurement gate.

---

## Interrupt Architecture

Two interrupts drive the firmware.

### Pin‑Change Interrupt (PCINT)

Triggered by PB4 and PB2.

Responsibilities:

- Detect rising edges on input signals
- Increment frequency counters

### Timer0 Compare Interrupt

Triggered approximately every 16 ms.

Responsibilities:

- Maintain the 300 ms measurement gate
- Snapshot frequency counters
- Execute clutch control logic

---

## Power Behavior

- Clutch output initializes **LOW** at power‑up.
- Clutch remains disengaged until the first valid decision cycle completes.

---

## Execution Model

The firmware is entirely interrupt‑driven.

The main loop simply sleeps:

```
while(1)
{
    sleep_mode();
}
```

The CPU wakes only for interrupts, minimizing power consumption and
reducing timing jitter.

---

## Build

Example compilation:

```bash
avr-gcc -mmcu=attiny85 -std=c99 -DF_CPU=8000000UL -Os -o tcclc.elf tcclc.c
avr-objcopy -O ihex tcclc.elf tcclc.hex
```

---

## Programming (Atmel‑ICE)

Flash firmware:

```bash
avrdude -p t85 -c atmelice_isp -P usb -U flash:w:tcclc.hex:i
```

Program fuses:

```bash
avrdude -p t85 -c atmelice_isp -P usb \
-U lfuse:w:0xE2:m \
-U hfuse:w:0xDF:m \
-U efuse:w:0xFC:m
```

---

## Design Characteristics

### Advantages

- Deterministic timing
- Interrupt‑driven architecture
- Integer arithmetic only
- Low CPU load
- Robust startup behavior
- Very small flash footprint

### Limitations

- Frequency resolution limited by 300 ms gate
- Threshold accuracy depends on RC oscillator tolerance
- Speed thresholds quantized to integer pulse counts

---

## Summary

This project implements a compact and reliable torque converter clutch
controller using vehicle speed and engine RPM signals. The firmware
prioritizes deterministic behavior, simplicity, and minimal resource
usage while remaining well suited for embedded automotive applications.
