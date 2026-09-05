/*
 * ESP32 Bluetooth Audio Receiver
 * A2DP sink -> PCM5102A I2S DAC, connection status / track metadata shown
 * on a 16x2 I2C character LCD. A rotary encoder (with an integrated push
 * switch, shared with the control button) drives a small on-screen menu
 * for unpairing and independently adjusting the connect/disconnect/startup
 * notification volumes.
 */

#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <ESP_I2S.h>
#include <LiquidCrystal_I2C.h>
#include <OneButton.h>
#include <Wire.h>

#include "NotifAudio.h"
#include "NotifVolume.h"
#include "RotaryEncoder.h"
#include "conn_hid_mp3.h"
#include "discon_hid_mp3.h"

// ---------------------------------------------------------------------
// Pin configuration
// ---------------------------------------------------------------------
/* I2S */
const uint8_t I2S_MCLK = -1;  /* Master clock (PCM5102A SCK) — unused, SCK pin is grounded on the module */
const uint8_t I2S_SCK = 26;   /* Audio data bit clock (PCM5102A BCK) */
const uint8_t I2S_WS = 25;    /* Audio data left and right clock (PCM5102A LRCK) */
const uint8_t I2S_SDOUT = 27; /* ESP32 audio data output (to speakers / PCM5102A DIN) */
const uint8_t I2S_SDIN = -1;  /* ESP32 audio data input (from microphone) — not used */

/* Button - also the rotary encoder's own integrated push-switch. One
   physical button serves both roles through this single OneButton instance. */
const uint8_t BUTTON_PIN = 39;   // active LOW (GPIO34-39 are input-only, no internal pull resistor)

// Hold past MENU_HOLD_MS to open the on-screen menu from the normal
// display, or to back out one level from anywhere inside it - see
// onControlLongPressStart().
const unsigned long MENU_HOLD_MS = 2000;

/* Rotary encoder (menu navigation + notification volumes) */
const uint8_t ENCODER_DT_PIN = 34;
const uint8_t ENCODER_CLK_PIN = 36;

// Like BUTTON_PIN above, GPIO34/36 are input-only pins on the classic ESP32
// with no internal pull resistor. Many encoder breakout boards (KY-040 and
// similar) already carry their own onboard pull-ups; if yours doesn't, wire
// an external 10K pull-up to 3.3V on both, the same way BUTTON_PIN is
// already wired.

// Each encoder step changes the notification volume by this many
// percentage points.
const uint8_t VOLUME_STEP_PERCENT = 5;

// If clockwise turns move the menu cursor up instead of down (or lower the
// volume instead of raising it), your encoder's A/B phases are swapped
// relative to DT/CLK - flip this one line rather than rewiring anything.
const bool ENCODER_INVERT_DIRECTION = false;

// How long the menu (or any of its sub-screens) can sit idle before it
// auto-returns to the normal display. Any pending volume change is saved
// first, exactly as if the user had confirmed it with a click.
const unsigned long MENU_TIMEOUT_MS = 15000;

/* I2C */
const uint8_t LCD_SDA_PIN = 21;  // I2C bus pins for the LCD
const uint8_t LCD_SCL_PIN = 22;

// Most PCF8574 16x2 I2C backpacks use 0x27 (some use 0x3F) - run an I2C
// scanner sketch and update this if the LCD stays blank.
const uint8_t LCD_I2C_ADDR = 0x27;
const uint32_t LCD_I2C_FREQ = 400000;
const uint8_t LCD_COLS = 16;
const uint8_t LCD_ROWS = 2;

LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
I2SClass i2s;
BluetoothA2DPSink a2dp_sink(i2s);
OneButton controlButton;  // GPIO39, configured in setup()

String current_artist = "";
String current_title = "";

// Set from connection_state_callback(), which runs on a different FreeRTOS
// task than loop(); volatile avoids a stale cached value across iterations.
volatile bool is_connected = false;

// Last connection state a sound was played for, so loop() plays
// conn_hid_mp3 / discon_hid_mp3 exactly once per transition.
bool sound_last_connected = false;

// Desired playback state (this sketch's own intent), flipped instantly by
// onControlClick() and reconciled with the real A2DP state in the
// background by audio_state_callback(). See onControlClick() for why.
volatile bool music_is_playing = true;

// Per-row scrolling state: index 0 = artist (row 0), index 1 = title (row 1)
int scroll_pos[2] = {0, 0};
unsigned long last_scroll_ms[2] = {0, 0};
const unsigned long SCROLL_INTERVAL_MS = 400; // ms per character step

// Last text written to each row, so the LCD is only rewritten when the
// content changes (avoids flicker and needless I2C traffic).
String lcd_row_shown[2] = {"", ""};

// ---------------------------------------------------------------------
// On-screen menu state
// ---------------------------------------------------------------------

// UI_NORMAL is the existing connection/track display. The others are the
// menu and its two sub-screens, reachable only via BUTTON_PIN (click =
// select/confirm, hold past MENU_HOLD_MS = open the menu / back out one
// level) and ENCODER_DT_PIN/ENCODER_CLK_PIN (rotate = move cursor / change
// volume). See onControlClick() and onControlLongPressStart() for the full
// state chart.
enum UiState : uint8_t {
  UI_NORMAL = 0,
  UI_MENU_LIST,
  UI_VOLUME_SETTING,
  UI_CONFIRM_UNPAIR,
};
UiState ui_state = UI_NORMAL;

// Each menu item is either a link to one independently-adjustable
// notification volume (isVolumeSlot=true, see NotifVolume.h) or a plain
// action/navigation entry (Unpair, Back). For the latter, .slot is never
// read (onControlClick() only consults it when isVolumeSlot is true) - it's
// set to NOTIF_VOL_SLOT_COUNT, the one enum value guaranteed not to be a
// real slot, so it can't be mistaken for an actual link to that volume.
struct MenuItem {
  const char* label;
  bool isVolumeSlot;
  NotifVolumeSlot slot;
};

const MenuItem MENU_ITEMS[] = {
  {"Connect Vol",    true,  NOTIF_VOL_CONNECT},
  {"Disconnect Vol", true,  NOTIF_VOL_DISCONNECT},
  {"Startup Vol",    true,  NOTIF_VOL_STARTUP},
  {"Unpair Device",  false, NOTIF_VOL_SLOT_COUNT},  // slot not applicable
  {"Back",           false, NOTIF_VOL_SLOT_COUNT},  // slot not applicable
};
const uint8_t MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
const uint8_t MENU_IDX_UNPAIR = 3;
const uint8_t MENU_IDX_BACK = 4;
uint8_t menu_cursor = 0;

// Which slot UI_VOLUME_SETTING is currently showing/adjusting - set from
// MENU_ITEMS[menu_cursor].slot when entering that screen, in onControlClick().
NotifVolumeSlot current_volume_slot = NOTIF_VOL_CONNECT;

// Tracks the last time the user did anything menu-related, so the menu can
// auto-close after MENU_TIMEOUT_MS of being left open and idle.
unsigned long last_menu_activity_ms = 0;

// A short-lived message (e.g. "Not Connected") shown centered on row 0 in
// place of whatever ui_state would otherwise render, then automatically
// reverting - see showMessage() and updateDisplay().
String transient_message = "";
unsigned long transient_message_until_ms = 0;
const unsigned long MESSAGE_DISPLAY_MS = 1300;

// ---------------------------------------------------------------------
// LCD helpers
// ---------------------------------------------------------------------

// Pads/truncates text to LCD_COLS and writes the row, but only if it
// differs from what's already shown there.
void lcdPrintRow(uint8_t row, String text) {
  while ((int)text.length() < LCD_COLS) text += ' ';
  if ((int)text.length() > LCD_COLS) text = text.substring(0, LCD_COLS);

  if (text == lcd_row_shown[row]) return;
  lcd_row_shown[row] = text;

  lcd.setCursor(0, row);
  lcd.print(text);
}

String centered(const String &text) {
  if ((int)text.length() >= LCD_COLS) return text.substring(0, LCD_COLS);
  String out = "";
  for (int i = 0; i < (LCD_COLS - (int)text.length()) / 2; i++) out += ' ';
  out += text;
  return out;
}

// Marquee-scrolls text across the row; text that already fits is shown
// centered and static instead.
void updateScrollingRow(uint8_t row, const String &text) {
  if (text.length() == 0) {
    lcdPrintRow(row, "");
    return;
  }
  if ((int)text.length() <= LCD_COLS) {
    lcdPrintRow(row, centered(text));
    return;
  }

  String padded = text + "   "; // gap before the text loops back to the start
  int len = padded.length();

  if (millis() - last_scroll_ms[row] > SCROLL_INTERVAL_MS) {
    scroll_pos[row] = (scroll_pos[row] + 1) % len;
    last_scroll_ms[row] = millis();
  }

  String rotated = padded.substring(scroll_pos[row]) + padded.substring(0, scroll_pos[row]);
  lcdPrintRow(row, rotated.substring(0, LCD_COLS));
}

// ---------------------------------------------------------------------
// Notification sounds
// ---------------------------------------------------------------------

// Each clip is tagged with its own NotifVolumeSlot (see NotifVolume.h), so
// connect/disconnect/startup each play at their own independently-adjusted
// volume rather than sharing one level.
struct AudioTrack {
  const uint8_t* data;
  size_t len;
  NotifVolumeSlot slot;
};

const AudioTrack connectSound    = { conn_hid_mp3, conn_hid_mp3_len, NOTIF_VOL_CONNECT };
const AudioTrack disconnectSound = { discon_hid_mp3, discon_hid_mp3_len, NOTIF_VOL_DISCONNECT };
// No dedicated startup clip exists, so this plays the same audio data as
// disconnectSound (no phone can be connected yet at power-up anyway) - but
// it's tagged with its own NOTIF_VOL_STARTUP slot, so its volume can still
// be raised or lowered independently of the disconnect chime's.
const AudioTrack startupSound    = { discon_hid_mp3, discon_hid_mp3_len, NOTIF_VOL_STARTUP };

// Plays a notification clip at its own slot's notification volume, then
// restores I2S TX to the A2DP music format (44.1kHz/16-bit/stereo), since
// playMP3Volume() leaves TX set to whatever format the clip itself was
// encoded in. Blocks until playback finishes.
//
// These per-slot notification volumes are the ONLY volume this sketch ever
// adjusts - the A2DP sink/receiver itself is intentionally left at its
// default (maximum) volume throughout, exactly as requested; nothing here
// calls into any sink-volume API.
void playNotification(const AudioTrack &track) {
  playMP3Volume(i2s, track.data, track.len, notifVolume.get(track.slot));
  i2s.configureTX(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH);
}

// ---------------------------------------------------------------------
// A2DP callbacks
// ---------------------------------------------------------------------

void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
  String val = String((char*)text);
  if (id == 0x1) { current_title = val;  scroll_pos[1] = 0; }
  if (id == 0x2) { current_artist = val; scroll_pos[0] = 0; }
}

// Just records the state - this runs on the Bluetooth stack's ~3KB-stack
// task, too little for the MP3 decoder playNotification() needs. loop()
// detects the change and plays the sound from the main task instead.
void connection_state_callback(esp_a2d_connection_state_t state, void *ptr) {
  is_connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
}

// Keeps music_is_playing in sync when playback changes from the phone's own
// UI. Same small-stack task as above, so only the flag is set here.
void audio_state_callback(esp_a2d_audio_state_t state, void *ptr) {
  music_is_playing = (state == ESP_A2D_AUDIO_STATE_STARTED);
}

// ---------------------------------------------------------------------
// Menu helpers
// ---------------------------------------------------------------------

// Shows msg centered on row 0 for MESSAGE_DISPLAY_MS, overriding whatever
// ui_state would otherwise render. ui_state itself is left untouched, so
// whichever screen was showing resumes normally once the message expires.
void showMessage(const String &msg) {
  transient_message = msg;
  transient_message_until_ms = millis() + MESSAGE_DISPLAY_MS;
}

void enterMenu() {
  ui_state = UI_MENU_LIST;
  menu_cursor = 0;
  last_menu_activity_ms = millis();
}

void exitMenuToNormal() {
  ui_state = UI_NORMAL;
}

// ---------------------------------------------------------------------
// Button handling (GPIO39, via OneButton - shared with the encoder's own
// push-switch)
// ---------------------------------------------------------------------

// Short click. What it does depends on which screen is currently shown.
void onControlClick() {
  switch (ui_state) {
    case UI_NORMAL:
      // Play/Pause - unchanged from before the menu existed.
      //
      // Uses music_is_playing rather than a2dp_sink.get_audio_state(),
      // which lags behind until the phone confirms the change over
      // Bluetooth - deciding from a stale value could send the same
      // command twice on a quick double click. audio_state_callback()
      // reconciles the flag with the real state later.
      if (music_is_playing) {
        a2dp_sink.pause();
        music_is_playing = false;
      } else {
        a2dp_sink.play();
        music_is_playing = true;
      }
      break;

    case UI_MENU_LIST:
      last_menu_activity_ms = millis();
      if (MENU_ITEMS[menu_cursor].isVolumeSlot) {
        current_volume_slot = MENU_ITEMS[menu_cursor].slot;
        ui_state = UI_VOLUME_SETTING;
      } else if (menu_cursor == MENU_IDX_UNPAIR) {
        if (is_connected) {
          ui_state = UI_CONFIRM_UNPAIR;
        } else {
          showMessage("Not Connected");
        }
      } else {  // MENU_IDX_BACK
        exitMenuToNormal();
      }
      break;

    case UI_VOLUME_SETTING:
      // Explicit confirm: save right away rather than waiting out
      // NotifVolume's debounce window, then go back up to the list.
      notifVolume.commitNow();
      ui_state = UI_MENU_LIST;
      last_menu_activity_ms = millis();
      break;

    case UI_CONFIRM_UNPAIR:
      if (is_connected) a2dp_sink.disconnect();
      exitMenuToNormal();
      break;
  }
}

// Fires once, immediately, the moment a hold crosses MENU_HOLD_MS (while
// the button is still down - see attachLongPressStart() in setup()).
// Acts as "open the menu" from the normal display, and as "back out one
// level" from anywhere inside it.
void onControlLongPressStart() {
  switch (ui_state) {
    case UI_NORMAL:
      enterMenu();
      break;

    case UI_MENU_LIST:
      exitMenuToNormal();
      break;

    case UI_VOLUME_SETTING:
      notifVolume.commitNow();
      ui_state = UI_MENU_LIST;
      last_menu_activity_ms = millis();
      break;

    case UI_CONFIRM_UNPAIR:
      ui_state = UI_MENU_LIST;  // cancelled - nothing gets disconnected
      last_menu_activity_ms = millis();
      break;
  }
}

// ---------------------------------------------------------------------
// Rotary encoder handling
// ---------------------------------------------------------------------

// Applies accumulated encoder ticks according to the current screen: moves
// the menu cursor in UI_MENU_LIST, adjusts volume in UI_VOLUME_SETTING.
// Rotation does nothing in UI_NORMAL or UI_CONFIRM_UNPAIR, but the counter
// is still drained on every call so ticks made while it's ignored don't
// pile up and jump the cursor/volume later.
void processEncoder() {
  int32_t delta = rotaryEncoder.readDelta();
  if (delta == 0) return;

  if (ui_state == UI_MENU_LIST) {
    last_menu_activity_ms = millis();
    // Clockwise (positive delta) moves the cursor down; wraps at both
    // ends. Modulo (rather than clamping to +/-1) means a fast spin that
    // accumulated several detents between loop() passes still moves the
    // cursor by that same number of steps.
    int newCursor = ((int)menu_cursor + delta) % (int)MENU_ITEM_COUNT;
    if (newCursor < 0) newCursor += MENU_ITEM_COUNT;
    menu_cursor = (uint8_t)newCursor;

  } else if (ui_state == UI_VOLUME_SETTING) {
    last_menu_activity_ms = millis();
    int newVolume = (int)notifVolume.get(current_volume_slot) + delta * (int)VOLUME_STEP_PERCENT;
    newVolume = constrain(newVolume, 0, 100);
    notifVolume.set(current_volume_slot, (uint8_t)newVolume);
  }
}

// ---------------------------------------------------------------------
// Display rendering
// ---------------------------------------------------------------------

void renderNormal(unsigned long current_ms) {
  if (!is_connected) {
    lcdPrintRow(0, centered("BT AUDIO"));
    if ((current_ms / 1000) % 2 == 0) lcdPrintRow(1, centered("connect phone"));
    else                              lcdPrintRow(1, "");
  } else {
    updateScrollingRow(0, current_artist);
    updateScrollingRow(1, current_title);
  }
}

// Shows a 2-row scrolling window into MENU_ITEMS with a ">" cursor on the
// selected line, keeping the cursor visible as it moves past the edge of
// the window. Purely a function of menu_cursor, so it needs no window
// position of its own to track between calls.
void renderMenuList() {
  int windowStart = (int)menu_cursor - (int)(LCD_ROWS - 1);
  windowStart = constrain(windowStart, 0, (int)MENU_ITEM_COUNT - (int)LCD_ROWS);

  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    uint8_t itemIdx = windowStart + row;
    String line = (itemIdx == menu_cursor) ? ">" : " ";
    line += MENU_ITEMS[itemIdx].label;
    lcdPrintRow(row, line);
  }
}

// Row 0 shows which of the 3 independent volumes this is (whichever menu
// item was highlighted to get here - see current_volume_slot). Row 1 is
// exactly 16 characters: a 3-digit right-justified percentage, "%[", a
// 10-segment bar, "]" - e.g. " 75%[#######---]".
void renderVolumeSetting() {
  lcdPrintRow(0, centered(MENU_ITEMS[menu_cursor].label));

  uint8_t pct = notifVolume.get(current_volume_slot);
  uint8_t filled = pct / 10;

  char prefix[6];
  snprintf(prefix, sizeof(prefix), "%3u%%[", pct);

  String line = prefix;
  for (uint8_t i = 0; i < 10; i++) line += (i < filled) ? '#' : '-';
  line += ']';
  lcdPrintRow(1, line);
}

void renderConfirmUnpair() {
  lcdPrintRow(0, centered("Unpair device?"));
  lcdPrintRow(1, "Clk=Yes Hold=No");
}

void updateDisplay(unsigned long current_ms) {
  if (transient_message.length() > 0) {
    if (current_ms < transient_message_until_ms) {
      lcdPrintRow(0, centered(transient_message));
      lcdPrintRow(1, "");
      return;
    }
    transient_message = "";
  }

  switch (ui_state) {
    case UI_NORMAL:         renderNormal(current_ms); break;
    case UI_MENU_LIST:      renderMenuList();          break;
    case UI_VOLUME_SETTING: renderVolumeSetting();     break;
    case UI_CONFIRM_UNPAIR: renderConfirmUnpair();     break;
  }
}

// ---------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------

void setup() {
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN, LCD_I2C_FREQ);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // GPIO39 (like all of GPIO34-39) has no internal pull resistor, so INPUT
  // relies on an external 10K pull-up to 3.3V; activeLow=true matches a GND press.
  controlButton.setup(BUTTON_PIN, INPUT, true);
  controlButton.setPressMs(MENU_HOLD_MS);
  controlButton.attachClick(onControlClick);
  controlButton.attachLongPressStart(onControlLongPressStart);

  rotaryEncoder.begin(ENCODER_DT_PIN, ENCODER_CLK_PIN);
  rotaryEncoder.setInvertDirection(ENCODER_INVERT_DIRECTION);

  // Load the persisted notification volumes before anything plays a sound.
  notifVolume.begin();

  // 16-bit stereo output to the PCM5102A at the standard A2DP rate.
  i2s.setPins(I2S_SCK, I2S_WS, I2S_SDOUT, I2S_SDIN, I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    lcdPrintRow(0, centered("I2S init"));
    lcdPrintRow(1, centered("failed!"));
    while (1);  // halt, no audio output is possible
  }

  playNotification(startupSound);

  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  a2dp_sink.set_on_connection_state_changed(connection_state_callback);
  a2dp_sink.set_on_audio_state_changed(audio_state_callback);
  a2dp_sink.start("BT AUDIO");
}

void loop() {
  unsigned long current_ms = millis();

  // Drives OneButton's state machine and fires onControlClick()/
  // onControlLongPressStart() here; must run every pass with no delay() in
  // between.
  controlButton.tick();

  // Play the matching clip exactly once per connect/disconnect transition,
  // regardless of which screen is currently shown.
  if (is_connected != sound_last_connected) {
    sound_last_connected = is_connected;
    if (sound_last_connected) playNotification(connectSound);
    else                      playNotification(disconnectSound);
  }

  // See processEncoder()'s comment on why this always runs, even when
  // rotation is currently meaningless.
  processEncoder();

  // Debounced NVS write - see NotifVolume.h/.cpp for why this is safe to
  // call unconditionally on every pass (it only actually touches flash
  // once a change has been stable for a second).
  notifVolume.commitIfDue();

  // Auto-close the menu (saving any pending volume change first) if it's
  // been left open and idle.
  if (ui_state != UI_NORMAL && (current_ms - last_menu_activity_ms > MENU_TIMEOUT_MS)) {
    notifVolume.commitNow();
    ui_state = UI_NORMAL;
  }

  updateDisplay(current_ms);
}
