#pragma once
// Volume-controllable drop-in for I2SClass::playMP3(), which has no gain
// stage of its own. Reimplements the same decode loop with the public Helix
// decoder API (mp3dec.h) and scales each PCM sample before writing to I2S -
// ESP_I2S.{h,cpp} and esp-libhelix-mp3 itself are left untouched.
//
// mp3dec.h needs no extra library entry: arduino-esp32 already depends on
// chmorgan/esp-libhelix-mp3 for its own playMP3(), so the include path is
// already available here too.
#include <ESP_I2S.h>

// Like i2s.playMP3(data, len), but scales every decoded sample by
// volumePercent (100 = original level, 0 = silent; values above 100 are
// clamped, since this is for attenuation, not amplification).
//
// Blocks until playback finishes and, like playMP3(), leaves I2S TX
// configured for the clip's own format - reconfigure TX afterwards if a
// fixed-format stream (e.g. A2DP) follows.
//
// Returns false, matching playMP3(), if decoding fails.
bool playMP3Volume(I2SClass &i2s, const uint8_t *data, size_t len, uint8_t volumePercent);
