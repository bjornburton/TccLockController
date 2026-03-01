# powerPot
A two channel solution that converts the voltage from potentiometers to PWM outputs for heavier
loads. This is helpful when power-pots are too large, expensive, hot or hard to get.
The load just needs a switching device like a transistor.
Since the switch does not operate in a partialy on state, it disipates very
little power.

The potentiometer is connected across VCC, be it 3.3 or 5 Volts.
The wiper of the pot can then have any voltage from 0 to VCC.
The wiper is connected to the multiplexer. The MUX then selects this input and
connects it to the ADC.
The ADC is configured to use VCC as its reference voltage and to provide only
8 bits (0 -- 255).
For those reasons the ADC resolves 0 to VCC to an integer over the range of
0 to 255.
That integer is fed to the PWM timer.
