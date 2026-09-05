#pragma once
// Persists an INDEPENDENT volume per notification clip (connect/disconnect/
// startup chimes) to NVS flash via the Preferences library - each one is a
// separate, separately-adjustable setting, not one shared level.
//
// This is deliberately NOT the Bluetooth sink/receiver's own audio volume -
// that stays untouched at maximum, as this project intends. These values
// only feed the separate gain stage in playMP3Volume() (see NotifAudio.h)
// that scales the notification clips themselves.
//
// NVS flash has a limited write-cycle lifetime, so writes are deliberately
// NOT immediate: set() only updates the in-RAM value and starts a debounce
// timer for that one slot. Call commitIfDue() every loop() pass (it saves a
// given slot at most once per change, after it's been stable for a second)
// and commitNow() on an explicit user confirmation (e.g. leaving the volume
// screen), so a change is never lost to a power cut but flash is never
// hammered on every single encoder detent.
#include <Arduino.h>

constexpr uint8_t NOTIF_VOLUME_DEFAULT = 100;  // max volume, per spec

// One entry per independently-adjustable notification clip. Add a slot here
// (and to kVolumeKeys in NotifVolume.cpp) if another notification clip ever
// needs its own volume.
enum NotifVolumeSlot : uint8_t {
  NOTIF_VOL_CONNECT = 0,
  NOTIF_VOL_DISCONNECT,
  NOTIF_VOL_STARTUP,
  NOTIF_VOL_SLOT_COUNT  // not a real slot - number of slots above
};

class NotifVolumeStore {
public:
  NotifVolumeStore();

  // Loads all slots from NVS, or NOTIF_VOLUME_DEFAULT for any slot with no
  // value saved yet (fresh device / cleared flash). Safe to call even if the
  // NVS namespace doesn't exist yet - Preferences::begin() simply fails and
  // getUChar() falls back to the supplied default in that case.
  void begin();

  uint8_t get(NotifVolumeSlot slot) const {
    return _percent[slot];
  }

  // Clamps to 0-100. If the value actually changed, marks that slot dirty
  // and (re)starts its own save-debounce timer - does NOT touch NVS itself.
  void set(NotifVolumeSlot slot, uint8_t percent);

  // Call every loop() iteration. A no-op for any slot that isn't dirty, or
  // whose value hasn't yet been stable for kSaveDebounceMs - see the .cpp.
  void commitIfDue();

  // Saves every currently-dirty slot immediately. Intended for an explicit
  // user action (confirm/back out of the volume screen) rather than routine
  // polling, so a change is captured right away instead of waiting out the
  // debounce window.
  void commitNow();

private:
  void save(NotifVolumeSlot slot);

  uint8_t _percent[NOTIF_VOL_SLOT_COUNT];
  bool _dirty[NOTIF_VOL_SLOT_COUNT];
  unsigned long _lastChangeMs[NOTIF_VOL_SLOT_COUNT];
};

// Single global instance, matching this sketch's convention for stateful
// peripherals/services (see controlButton, lcd, a2dp_sink in the main .ino).
extern NotifVolumeStore notifVolume;
