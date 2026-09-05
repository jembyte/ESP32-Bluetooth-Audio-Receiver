#pragma once
// Interrupt-driven quadrature rotary encoder reader.
//
// Only CLK is wired to an interrupt. A typical incremental encoder (e.g. a
// KY-040-style module) pulses CLK low exactly once per mechanical detent, so
// triggering on CLK's falling edge and sampling DT at that instant yields a
// clean +1/-1 per detent without needing full 4x quadrature decoding, which
// menu navigation doesn't need anyway.
#include <Arduino.h>

class RotaryEncoder {
public:
  // dtPin/clkPin may be input-only pins with no internal pull resistor
  // (e.g. GPIO34/36 on the classic ESP32, same restriction as GPIO34-39 in
  // general). Many encoder breakout boards (KY-040 and similar) already
  // carry their own onboard pull-ups; if yours doesn't, wire an external
  // 10K pull-up to 3.3V on both pins, the same way the control button's
  // GPIO39 already needs one.
  void begin(uint8_t dtPin, uint8_t clkPin);

  // Returns the signed number of detents accumulated since the last call
  // (positive = clockwise, negative = counter-clockwise) and resets the
  // internal counter to zero. Call this once per loop() pass regardless of
  // UI state - even when rotation is meant to be ignored, draining the
  // counter every pass prevents a backlog of ignored ticks from suddenly
  // being applied later (e.g. right after opening the menu).
  int32_t readDelta();

  // Flip this if clockwise/counter-clockwise come out reversed for your
  // physical wiring - see the .cpp comment above the ISR for why this can't
  // be known in software ahead of time.
  void setInvertDirection(bool invert) { _invert = invert; }

private:
  bool _invert = false;
};

// Single global instance, matching this sketch's convention for hardware
// peripherals (see controlButton, lcd, a2dp_sink in the main .ino).
extern RotaryEncoder rotaryEncoder;
