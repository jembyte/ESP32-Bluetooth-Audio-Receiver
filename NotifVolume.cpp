#include "NotifVolume.h"

#include <Preferences.h>

NotifVolumeStore notifVolume;

namespace {
const char *const kNamespace = "btaudio";

// One key per NotifVolumeSlot, same order as the enum. NVS key names must be
// 15 characters or fewer - all of these are well under that.
const char *const kVolumeKeys[NOTIF_VOL_SLOT_COUNT] = {
    "notifVolConn",   // NOTIF_VOL_CONNECT
    "notifVolDisc",   // NOTIF_VOL_DISCONNECT
    "notifVolStart",  // NOTIF_VOL_STARTUP
};

// See the .h comment on commitIfDue()/set() for why this is debounced
// rather than written on every change.
const unsigned long kSaveDebounceMs = 1000;
}  // namespace

NotifVolumeStore::NotifVolumeStore() {
  for (uint8_t i = 0; i < NOTIF_VOL_SLOT_COUNT; i++) {
    _percent[i] = NOTIF_VOLUME_DEFAULT;
    _dirty[i] = false;
    _lastChangeMs[i] = 0;
  }
}

void NotifVolumeStore::begin() {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/true);
  for (uint8_t i = 0; i < NOTIF_VOL_SLOT_COUNT; i++) {
    _percent[i] = prefs.getUChar(kVolumeKeys[i], NOTIF_VOLUME_DEFAULT);
    _dirty[i] = false;
  }
  prefs.end();
}

void NotifVolumeStore::set(NotifVolumeSlot slot, uint8_t percent) {
  if (percent > 100) percent = 100;
  if (percent == _percent[slot]) return;  // nothing to persist
  _percent[slot] = percent;
  _dirty[slot] = true;
  _lastChangeMs[slot] = millis();
}

void NotifVolumeStore::commitIfDue() {
  for (uint8_t i = 0; i < NOTIF_VOL_SLOT_COUNT; i++) {
    if (_dirty[i] && (millis() - _lastChangeMs[i] >= kSaveDebounceMs)) {
      save((NotifVolumeSlot)i);
    }
  }
}

void NotifVolumeStore::commitNow() {
  for (uint8_t i = 0; i < NOTIF_VOL_SLOT_COUNT; i++) {
    if (_dirty[i]) save((NotifVolumeSlot)i);
  }
}

void NotifVolumeStore::save(NotifVolumeSlot slot) {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putUChar(kVolumeKeys[slot], _percent[slot]);
  prefs.end();
  _dirty[slot] = false;
}
