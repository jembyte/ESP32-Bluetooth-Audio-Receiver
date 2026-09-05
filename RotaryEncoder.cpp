#include "RotaryEncoder.h"

RotaryEncoder rotaryEncoder;

namespace {
uint8_t s_dtPin = 0;

// Written from the ISR, read (and reset) from readDelta() in loop() -
// volatile so neither side works from a stale cached copy.
volatile int32_t s_delta = 0;
volatile unsigned long s_lastIsrUs = 0;

// A mechanical encoder can't physically produce two genuine detents this
// close together by hand, so this exists purely to swallow contact bounce.
// If turns still get double-counted on your specific hardware, raising this
// (or adding a small hardware RC filter on CLK/DT) is the usual fix.
const unsigned long kDebounceUs = 2000;

// IRAM_ATTR keeps this resident in IRAM so it stays safely callable even if
// flash cache is briefly disabled elsewhere - which happens for real here,
// since NotifVolume commits to NVS flash right around the time the user is
// likely still turning the knob.
void IRAM_ATTR onClkFalling() {
  unsigned long nowUs = micros();
  if (nowUs - s_lastIsrUs < kDebounceUs) return;
  s_lastIsrUs = nowUs;

  // Whether "DT high while CLK falls" means clockwise or counter-clockwise
  // depends on how the encoder's A/B phases happen to be wired to DT/CLK -
  // there's no universal answer, so this is calibrated once in the field via
  // setInvertDirection() rather than guessed here.
  if (digitalRead(s_dtPin) == HIGH) {
    s_delta++;
  } else {
    s_delta--;
  }
}
}  // namespace

void RotaryEncoder::begin(uint8_t dtPin, uint8_t clkPin) {
  s_dtPin = dtPin;
  pinMode(dtPin, INPUT);
  pinMode(clkPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(clkPin), onClkFalling, FALLING);
}

int32_t RotaryEncoder::readDelta() {
  noInterrupts();
  int32_t delta = s_delta;
  s_delta = 0;
  interrupts();
  return _invert ? -delta : delta;
}
