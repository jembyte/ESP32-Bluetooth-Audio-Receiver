#include "NotifAudio.h"

// Same check ESP_I2S.h uses for I2SClass::playMP3(); if it were ever false,
// the project's existing i2s.playMP3() calls would already fail to build.
#if __has_include("mp3dec.h")
#include "mp3dec.h"

bool playMP3Volume(I2SClass &i2s, const uint8_t *src, size_t src_len, uint8_t volumePercent) {
  if (volumePercent > 100) {
    volumePercent = 100;
  }
  // Q15 fixed-point gain (0..32768 for 0..100%) avoids float math per sample.
  const int32_t gainQ15 = ((int32_t)volumePercent * 32768) / 100;

  int16_t outBuf[MAX_NCHAN * MAX_NGRAN * MAX_NSAMP];
  const uint8_t *readPtr = src;
  int bytesAvailable = (int)src_len;
  int err = 0, offset = 0;
  MP3FrameInfo frameInfo;

  HMP3Decoder decoder = MP3InitDecoder();
  if (decoder == NULL) {
    log_e("Could not allocate decoder");
    return false;
  }

  do {
    offset = MP3FindSyncWord((unsigned char *)readPtr, bytesAvailable);
    if (offset < 0) {
      break;
    }
    readPtr += offset;
    bytesAvailable -= offset;

    err = MP3Decode(decoder, (unsigned char **)&readPtr, &bytesAvailable, outBuf, 0);
    if (err) {
      log_e("Decode ERROR: %d", err);
      MP3FreeDecoder(decoder);
      return false;
    }

    MP3GetLastFrameInfo(decoder, &frameInfo);

    // At 100% skip scaling entirely for a bit-exact passthrough.
    if (volumePercent != 100) {
      for (int i = 0; i < frameInfo.outputSamps; i++) {
        // Product fits int32_t and the >>15 result stays in int16_t range,
        // so no clamp is needed here.
        outBuf[i] = (int16_t)(((int32_t)outBuf[i] * gainQ15) >> 15);
      }
    }

    i2s.configureTX(frameInfo.samprate, (i2s_data_bit_width_t)frameInfo.bitsPerSample, (i2s_slot_mode_t)frameInfo.nChans);
    i2s.write((uint8_t *)outBuf, (size_t)((frameInfo.bitsPerSample / 8) * frameInfo.outputSamps));
  } while (true);

  MP3FreeDecoder(decoder);
  return true;
}

#else
#error "mp3dec.h not reachable from this translation unit - see the comment in NotifAudio.h"
#endif
