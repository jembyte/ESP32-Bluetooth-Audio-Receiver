# ESP32 Bluetooth Audio Receiver

This is a high-quality Bluetooth A2DP audio sink (receiver) based on the ESP32. It is designed to turn any vintage radio or speaker system into a modern wireless device with high-fidelity sound and a simple text display.

## ✨ Features
* **Premium Sound:** Utilizes the I2S protocol with an external **PCM5102 DAC** for 24-bit audio quality.
* **Simple Interface:** A 16x2 character LCD shows connection status and a blinking *"connect phone"* prompt while idle.
* **Metadata Display:** Shows the current track title and artist on the LCD, scrolling automatically when the text is longer than one line.
* **On-Screen Menu:** A rotary encoder (with an integrated push-switch, doubling as the control button) drives a small menu for unpairing the connected device and independently adjusting the connect/disconnect/startup notification volumes - separate from the Bluetooth sink's own audio volume, which is always left at maximum.

---

## 🏗 Hardware Requirements
* **Microcontroller:** ESP32-WROOM-32U WiFi Bluetooth Module ESP32 CP2102 U.FL (or compatible ESP32 board).
* **Audio DAC:** PCM5102A I2S Module.
* **Display:** LiquidCrystal I2C 16x2 (HD44780-compatible, PCF8574 I2C backpack).
* **Control:** 1x Rotary Encoder with an integrated push-switch (e.g. KY-040 or similar).

---

## 🔌 Connection Diagram

<table style="width: 100%; border-collapse: collapse; font-family: sans-serif; background-color: #0d1117; color: #e6edf3; border: 1px solid #30363d;">
  <thead>
    <tr style="background-color: #161b22; color: #58a6ff;">
      <th style="padding: 12px; border: 1px solid #30363d; text-align: left;">Module / Device</th>
      <th style="padding: 12px; border: 1px solid #30363d; text-align: left;">Pin Function</th>
      <th style="padding: 12px; border: 1px solid #30363d; text-align: center;">ESP32 Connection</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td rowspan="3" style="padding: 12px; border: 1px solid #30363d; font-weight: bold; color: #ff7b72; vertical-align: top;">PCM5102 (DAC)</td>
      <td style="padding: 8px; border: 1px solid #30363d;">BCK (Bit Clock)</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-family: monospace; font-weight: bold; color: #ff7b72;">GPIO 26</td>
    </tr>
    <tr>
      <td style="padding: 8px; border: 1px solid #30363d;">LRCK (Word Select)</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-family: monospace; font-weight: bold; color: #ff7b72;">GPIO 25</td>
    </tr>
    <tr>
      <td style="padding: 8px; border: 1px solid #30363d;">DIN (Data In)</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-family: monospace; font-weight: bold; color: #ff7b72;">GPIO 27</td>
    </tr>
    <tr>
      <td rowspan="2" style="padding: 12px; border: 1px solid #30363d; font-weight: bold; color: #79c0ff; vertical-align: top;">LCD 16x2 (I2C)</td>
      <td style="padding: 8px; border: 1px solid #30363d;">SCL (Clock)</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-family: monospace; font-weight: bold; color: #79c0ff;">GPIO 22</td>
    </tr>
    <tr>
      <td style="padding: 8px; border: 1px solid #30363d;">SDA (Data)</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-family: monospace; font-weight: bold; color: #79c0ff;">GPIO 21</td>
    </tr>
    <tr style="background-color: #161b22;">
      <td rowspan="3" style="padding: 12px; border: 1px solid #30363d; font-weight: bold; color: #7ee787; vertical-align: top;">ROTARY ENCODER</td>
      <td style="padding: 8px; border: 1px solid #30363d;">DT</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-family: monospace; font-weight: bold; color: #7ee787;">GPIO 34</td>
    </tr>
    <tr style="background-color: #161b22;">
      <td style="padding: 8px; border: 1px solid #30363d;">CLK</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-family: monospace; font-weight: bold; color: #7ee787;">GPIO 36</td>
    </tr>
    <tr style="background-color: #161b22;">
      <td style="padding: 8px; border: 1px solid #30363d;">SW (push-switch)<br><small style="color: #8b949e;">(Short to GND)</small></td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-family: monospace; font-weight: bold; color: #7ee787;">GPIO 39 ↔ GND</td>
    </tr>
    <tr style="background-color: #161b22;">
      <td rowspan="3" style="padding: 12px; border: 1px solid #30363d; font-weight: bold; color: #d2a8ff; vertical-align: top;">POWER SUPPLY</td>
      <td style="padding: 8px; border: 1px solid #30363d;">VCC PCM5102</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-weight: bold; color: #ffa657;">5V / VIN</td>
    </tr>
    <tr style="background-color: #161b22;">
      <td style="padding: 8px; border: 1px solid #30363d;">VCC LCD</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-weight: bold; color: #ffa657;">5V (typ.)*</td>
    </tr>
    <tr style="background-color: #161b22;">
      <td style="padding: 8px; border: 1px solid #30363d;">Common Ground</td>
      <td style="padding: 8px; border: 1px solid #30363d; text-align: center; font-weight: bold; color: #8b949e;">GND (All)</td>
    </tr>
  </tbody>
</table>

<div style="background-color: #161b22; padding: 12px; font-size: 11px; color: #8b949e; text-align: center; border: 1px solid #30363d; border-top: none; line-height: 1.5; font-family: sans-serif;">
  GPIO 34, 36 and 39 are input-only pins with no internal pull-up/down resistor.<br>
  Many encoder breakout boards (KY-040 and similar) already carry their own onboard pull-ups for DT/CLK/SW - if yours doesn't, wire an external 10K resistor from each of GPIO 34, 36 and 39 to 3.3V.<br>
  The encoder's own push-switch is what connects GPIO 39 to GND - a separate button is no longer used.<br>
  *Most 16x2 I2C backpacks run on 5V — check your specific module's rating.
</div>

<div style="background-color: #21262d; padding: 12px; font-size: 11px; color: #8b949e; text-align: center; border: 1px solid #30363d; border-top: none; line-height: 1.5; font-family: sans-serif;">
  ⚠️ If turning the knob moves the menu cursor or notification volume the <em>opposite</em> way from what feels natural, your encoder's A/B phases are swapped relative to DT/CLK on this wiring. Flip <code>ENCODER_INVERT_DIRECTION</code> to <code>true</code> near the top of the .ino rather than rewiring anything.
</div>

---

## 🛠 Required Libraries
To compile this project, you need to install the following libraries in your Arduino IDE:
1. [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) by Phil Schatzmann
2. [LiquidCrystal_I2C](https://github.com/johnrickman/LiquidCrystal_I2C) (search "LiquidCrystal I2C" in the Arduino Library Manager)
3. [OneButton](https://github.com/mathertel/OneButton) by Matthias Hertel (search "OneButton" in the Arduino Library Manager)

Rotary encoder decoding (`RotaryEncoder.h/.cpp`) and the notification-volume store (`NotifVolume.h/.cpp`) are implemented directly in this project - no extra library install is needed for either. Volume persistence uses `Preferences.h`, which ships with the `arduino-esp32` core itself.

## 📝 Usage
1. Power up the ESP32.
2. Search for **"BT AUDIO"** in your phone's Bluetooth settings.
3. Once connected, the LCD will show the artist on line 1 and the track title on line 2, scrolling automatically if the text is longer than 16 characters.
4. **Short press** the encoder's push-switch to play/pause.
5. **Hold** the push-switch for about 2 seconds to open the on-screen menu:
   * **Rotate** clockwise/counter-clockwise to move the cursor down/up.
   * **Short press** to select the highlighted item.
   * **Hold** again to back out one level (or close the menu entirely from the top level).
   * **Connect Vol / Disconnect Vol / Startup Vol** - three independent volumes, one per notification chime, each adjustable in 5% steps and shown as a live bar; each value is saved automatically and separately. These are separate from the Bluetooth sink's own audio volume, which this project always leaves at maximum.
   * **Unpair Device** - asks for confirmation (short press = yes, hold = cancel) before disconnecting the currently connected phone.
   * **Back** - returns to the normal display.
   * The menu closes itself and returns to the normal display after about 15 seconds of inactivity.

> **Note:** the LCD uses the HD44780 character set, not full UTF-8. Track/artist names with accented or non-Latin characters may render as odd symbols — this is a display hardware limitation, not a code bug.
