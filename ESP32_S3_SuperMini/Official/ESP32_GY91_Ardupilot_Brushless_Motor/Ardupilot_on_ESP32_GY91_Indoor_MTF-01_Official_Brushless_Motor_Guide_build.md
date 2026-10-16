# ArduPilot on ESP32-S3 Super Mini — USB-TTL Indoor MTF-01 Build Guide

Oct 15, 2026 · @lukas

## At a glance

This guide builds the same indoor ArduCopter drone as your **flying WEMOS** (GY-91, MTF-01, FS-A8S, four plain-PWM ESCs) on an **ESP32-S3 Super Mini**, with a **USB-TTL adapter** on the S3's TX/RX pins for both flashing and Mission Planner. The `defaults.parm` is your WEMOS file with **one line added**; the `hwdef.dat` differs only where the Super Mini's pins force it.

> **Status and safety.** ArduPilot on ESP32 is experimental, and this S3 build has not flown yet. Keep props **off** until Part 8, fly tethered first, and never near people.

**Tested version.** Your WEMOS build: `git describe` = `ArduPilot-4.6.0-beta1-9022-g9dd2e0f350` (commit `9dd2e0f3`), ESP-IDF 5.3. These exact board files were test-built at that commit.

**How to use this guide:** work through the Parts in order. Each step ends with a **Check**; don't move on until it passes. In command blocks, a blank line separates commands, a line ending in `\` continues on the next line, and text after `#` is a comment.

**Why the USB-TTL makes this simple.** Your WEMOS reaches Mission Planner through its built-in USB-serial chip, wired to UART0. The Super Mini has no such chip: its USB-C port goes straight into the S3, which is why the earlier S3 attempt needed its own USB driver fix. Your USB-TTL adapter **is** that missing chip. Wired to TX (GPIO43) and RX (GPIO44), it makes the S3 talk to Mission Planner and to the flasher exactly the way your WEMOS does, with ArduPilot's stock USB and WiFi code. ArduPilot's own `esp32s3devkit` board uses the same two pins the same way.

**Everything that differs from your WEMOS, and why:**

| What | WEMOS (`esp32lukas6050`) | S3 Super Mini (`esp32s3lukas`) | Why |
| --- | --- | --- | --- |
| `MAV1_PARAMS` (the one line added to `defaults.parm`) | Not set (0) | **20** | Faster first parameter download over the USB-TTL after power-up (6.1) |
| Pins | I2C 23/18, motors 25/27/33/32, PPM 4, MTF-01 16/17 | I2C **12/13**, motors **4/5/6/7**, PPM **8**, MTF-01 **9/10** | The Super Mini only has GPIO1–13 and 43/44 on its header |
| Mission Planner and flashing | USB chip on the board | **USB-TTL adapter** on TX/RX (GPIO43/44), 115200 | The Super Mini has no USB-serial chip |
| Download mode for flashing | Automatic | **Hold BOOT, tap RST** (2.1) | The adapter has no auto-reset wires |
| Flash size setting | 4 MB (classic ESP32 default) | 4 MB via Fix 6 | ArduPilot's S3 default is 8 MB |
| SD card | Driver on (errors harmless) | Driver and file system off | The SD driver would claim GPIO2 on the S3 |
| WiFi | TCP 192.168.4.1:5760, `lukas-drone` | TCP 192.168.4.1:5760, `lukas-drone-s3` | Same mode; a different name so the two drones can't be mixed up |
| `MAV4_OPTIONS 2` | Set in Mission Planner | Set in Mission Planner (6.2) | Same as the WEMOS |

**The fixes** (all in Part 4):

- **Fixes 1–4, the same four as your WEMOS:** optical flow on (4.3), motors not stuck at idle (4.4), I2C IMU reader priority (4.5), ESC calibration from Mission Planner (4.6).
- **Fix 5, the compass line** (`probe_mpu9250` in `hwdef.dat`), the same as your WEMOS.
- **Fix 6, S3 only: 4 MB flash.** ArduPilot's S3 settings say 8 MB; on a 4 MB Super Mini the firmware stops at boot (4.7). This file is read only by ESP32-S3 boards.

**No USB or WiFi driver changes.** The USB-TTL replaces the earlier S3 attempt's Fixes 7, 8 and 9, so nothing here changes code your WEMOS or DevKit builds use.

## Part 1: Parts and wiring

The Super Mini's header has 15 usable pins: **GPIO1–GPIO13, TX (GPIO43) and RX (GPIO44)**. TX and RX now go to the USB-TTL adapter; everything else is wired as in the earlier S3 plan. The GY-91 still needs only four wires (VIN, GND, SDA, SCL).

### 1.1 Parts

| Part | Notes |  |
| --- | --- | --- |
| ESP32-S3 Super Mini | ESP32-S3FH4R2: 4 MB flash, BOOT and RST buttons |  |
| USB-TTL adapter (CH340, CP2102 or FT232) | **Set to 3.3 V logic** (jumper or switch). Your link for flashing and Mission Planner |  |
| GY-91 (MPU9250 + BMP280) | I2C. Part 2 checks the IMU, barometer and compass |  |
| MicoAir MTF-01 | Optical flow + rangefinder, MAVLink on a UART |  |
| FlySky FS-A8S receiver + transmitter | PPM output |  |
| 4 × brushless ESC (40A, 5V BEC) + A2212 1400KV + 8045 props | The same as the WEMOS drone |  |
| 3S LiPo | 11.1V nominal |  |

### 1.2 Pin table

| Device pin | Super Mini pin | Notes |
| --- | --- | --- |
| USB-TTL **TX** | **GPIO44 (RX)** | Crossed over: the adapter's TX goes to the S3's RX |
| USB-TTL **RX** | **GPIO43 (TX)** |  |
| USB-TTL GND | GND | Always connect: without it the signals have no reference |
| USB-TTL VCC / 5V / 3V3 | **not connected** | Power comes from USB-C or the BEC (1.3) |
| GY-91 VIN / GND | 5V / GND | VIN feeds the GY-91's own 3.3V regulator |
| GY-91 3V3 | not connected | The regulator's output: don't feed power into it |
| GY-91 SDA | **GPIO12** | I2C data |
| GY-91 SCL | **GPIO13** | I2C clock |
| GY-91 SDO/SAO, NCS, CSB | not connected | If the Part 2 scan shows 0x69 or 0x77, tie SDO/SAO to GND |
| FS-A8S PPM | GPIO8 | Use the PPM output |
| FS-A8S + / − | 5V / GND |  |
| MTF-01 TX | GPIO9 | Super Mini RX, ArduPilot SERIAL3 |
| MTF-01 RX | GPIO10 | Super Mini TX |
| MTF-01 VCC / GND | 5V / GND |  |
| Motor 1–4 signal | GPIO4, GPIO5, GPIO6, GPIO7 | ESC signal wires |
| ESC signal grounds | GND | All four |
| 5V supply | 5V / GND | One ESC's BEC (red + black). Leave the other three ESCs' red wires unconnected |

**The I2C pins in your wiring, in `hwdef.dat` (4.1) and in the Part 2 sketch must be the same: SDA GPIO12, SCL GPIO13.** If they differ, ArduPilot never finds the IMU, startup never finishes, and Mission Planner gets no heartbeat.

**Wiring schematic**

&#91;embedded content: Wiring schematic · Super Mini, USB-TTL, GY-91, MTF-01, receiver, 4 ESCs, power\]

Arrows show which way a signal travels; SDA and SCL carry data both ways. Every device's GND, the adapter's included, must join the same ground as the Super Mini.

### 1.3 Wiring rules

- **Adapter at 3.3 V logic.** At 5 V it can damage the S3's GPIO43/44.
- **One power source at a time.** On the bench, power the Super Mini from its USB-C port (a PC port or a phone charger; it now carries only power) with the BEC's red wire unplugged. On the drone, the BEC powers it and USB-C stays out. Never connect the adapter's VCC pin.
- **Keep the boot and USB pins free.** GPIO0 is the BOOT button, GPIO3 is sampled at reset, GPIO19/GPIO20 are the USB lines (not on the header), and GPIO48 drives the on-board LED.
- **Keep the GY-91 wires short** (under about 10 cm) and away from the ESC and battery wires.
- **Keep current-carrying wires away from the GY-91**: the compass is on that board, and battery and ESC power leads close to it pull the heading as the motors draw current.
- **Cover the BMP280** (the small metal can with a pin-hole) with open-cell foam. Don't seal it airtight.
- **Mount the GY-91 chip side up, its Y axis forward, X axis right** (`ROTATION_NONE`), flat, near the centre of the frame, on firm foam. 6.5 confirms it.
- **Mount the MTF-01** underneath, lens straight down, with a clear view of the floor.
- **Keep the antenna end of the Super Mini clear** of the battery, ESC power leads and metal: it's a small on-board antenna.

**Motor layout** (quad X, viewed from above, front at the top):

| Pin | Motor | Position | Spin | Motor Test button |
| --- | --- | --- | --- | --- |
| GPIO4 | 1 | front-right | CCW | A |
| GPIO5 | 2 | rear-left | CCW | C |
| GPIO6 | 3 | front-left | CW | D |
| GPIO7 | 4 | rear-right | CW | B |

**Check:** before powering up, confirm with a multimeter that 5V and GND aren't shorted, that nothing is wired to GPIO0, GPIO3 or GPIO48, and that the adapter's jumper is on 3.3 V.

## Part 2: The USB-TTL link, and the GY-91 test

### 2.1 Put the S3 in download mode and check the link

The S3 can be flashed through GPIO43/44, but only when it starts in **download mode**. Boards with a USB chip switch into it automatically; your adapter has no reset wires, so you do it with the two buttons:

**Way 1, BOOT while powering up** (the usual way):

1. Unplug the Super Mini's power (USB-C, or the BEC).
2. **Hold BOOT.**
3. **Plug the USB-C power back in.**
4. Release BOOT after about a second.

**Way 2, BOOT + RST** (when the board is already powered):

1. **Hold BOOT.**
2. **Press and release RST.**
3. **Release BOOT.**

Either way, the S3 now waits for the flasher on GPIO43/44: no program runs.

After any flash, **press RST once** to start the new program.

The adapter shows up in Windows Device Manager as **USB-SERIAL CH340 (COMx)**, **Silicon Labs CP210x (COMx)** or **USB Serial Port (COMx)**. If it doesn't, install its driver (CH340 or CP210x).

**Link check.** Put the S3 in download mode, then ask the chip who it is. In Windows (after `pip install esptool`), with your adapter's COM port:

```powershell
python -m esptool --chip esp32s3 --port COM7 --before no_reset flash_id
```

Or in Ubuntu (WSL users: attach the adapter first, 5.3):

```bash
esptool.py --chip esp32s3 -p /dev/ttyUSB0 --before no_reset flash_id
```

**Check:** it prints `Chip is ESP32-S3` and `Detected flash size: 4MB`. `Failed to connect`: TX and RX swapped, GND missing, the adapter not on 3.3 V, or the board not in download mode. Press RST to leave download mode.

### 2.2 Test the GY-91 before you build

This sketch proves four things before ArduPilot is involved: the GY-91 answers on your pins, its chip is an MPU9250, its compass answers once the I2C bypass is open, and its readings are quiet while it sits still.

**Arduino IDE settings for the USB-TTL** (Tools menu, board **ESP32S3 Dev Module**, ESP32 core 3.x):

| Setting | Value | Why |
| --- | --- | --- |
| Port | The adapter's COM port | Shows as CH340, CP210x or USB Serial Port |
| **USB CDC On Boot** | **Disabled** | The key setting: `Serial` goes out on TX/RX (GPIO43/44), to your adapter. Enabled sends it to USB-C and the Serial Monitor stays blank |
| **Upload Mode** | **UART0 / Hardware CDC** | Uploads through GPIO43/44 |
| Upload Speed | **921600** | If it fails partway (common on CH340), use 460800 |
| Flash Size | **4MB (32Mb)** | The Super Mini has 4 MB |
| Partition Scheme | Default 4MB with spiffs | Matches 4 MB |
| PSRAM | **Disabled** | The sketches don't need it. Never OPI PSRAM: the Super Mini's chip doesn't have it, and the sketch crashes at start |
| Flash Mode | QIO 80MHz (default) | If a sketch won't start after upload, try DIO 80MHz |
| CPU Frequency | 240MHz (WiFi) | Default |
| USB DFU On Boot, USB Firmware MSC On Boot, JTAG Adapter, Erase All Flash Before Sketch Upload | Disabled | Defaults; not used |

**Uploading** (the adapter has no auto-reset wires, so download mode is done by hand):

1. Put the S3 in download mode (2.1): hold BOOT while you plug in its USB-C power, then release.
2. Click **Upload**. Arduino may print a reset warning: ignore it, the board is already waiting.
3. When it says `Hard resetting via RTS pin...`, press **RST** yourself: that press starts the sketch.
4. Open the **Serial Monitor at 115200** (for the ESC sketch in 7.3, line ending **Newline**).

Put the board on a folded towel and don't touch it while it measures.

```cpp
// GY-91 check: I2C scan, chip ID, compass (AK8963) diagnosis, and a still-noise test.
// Board: ESP32-S3 Super Mini. Wiring: SDA = IO12, SCL = IO13, VCC, GND. Output on TX/RX: read it through the USB-TTL.
// Board on a towel, don't touch it.
#include <Wire.h>

const int SDA_PIN = 12, SCL_PIN = 13;
const uint8_t ADDR = 0x68;          // MPU9250, SDO/SAO low
const uint8_t MAG  = 0x0C;          // AK8963 inside the MPU9250
const int N = 500;                  // samples per test (about 2.5 s)
uint8_t who = 0;
bool magOk = false;
float asa[3] = {1, 1, 1};           // AK8963 factory sensitivity adjustment

bool writeTo(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

void writeReg(uint8_t reg, uint8_t val) { writeTo(ADDR, reg, val); }

bool readFrom(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;     // repeated start
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void scanBus(const char *title) {
  Serial.printf("\n=== I2C scan: %s ===\n", title);
  bool seen0C = false;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X", a);
      if (a == 0x0C) { Serial.printf("  <- AK8963 compass"); seen0C = true; }
      if (a == 0x68 || a == 0x69) Serial.printf("  <- MPU9250");
      if (a == 0x76 || a == 0x77) Serial.printf("  <- BMP280");
      Serial.println("");
    }
  }
  Serial.printf("  0x0C %s\n", seen0C ? "present" : "NOT present");
}

bool checkCompass() {
  uint8_t wia = 0;
  bool ok = readFrom(MAG, 0x00, &wia, 1);                  // AK8963 WIA register
  Serial.printf("Compass AK8963 at 0x0C: %s, WIA = 0x%02X (expect 0x48)\n",
                ok ? "answers" : "NOT found", wia);
  return ok && wia == 0x48;
}

void setupCompass() {
  uint8_t a[3] = {128, 128, 128};                          // 128 = no adjustment
  writeTo(MAG, 0x0A, 0x00);                                // CNTL1: power down
  delay(10);
  writeTo(MAG, 0x0A, 0x0F);                                // CNTL1: fuse ROM access
  delay(10);
  if (readFrom(MAG, 0x10, a, 3)) {                         // ASAX, ASAY, ASAZ
    for (int k = 0; k < 3; k++) asa[k] = (a[k] - 128) / 256.0f + 1.0f;
  }
  Serial.printf("Compass ASA = %u %u %u (factory trim, usually 150-190)\n", a[0], a[1], a[2]);
  writeTo(MAG, 0x0A, 0x00);                                // power down again
  delay(10);
  writeTo(MAG, 0x0A, 0x16);                                // 16-bit, continuous 100 Hz
  delay(10);
}

// reads one compass sample in microtesla; false if no new data
bool readCompass(float out[3]) {
  uint8_t st1 = 0, b[7];
  if (!readFrom(MAG, 0x02, &st1, 1) || !(st1 & 0x01)) return false;   // ST1 DRDY
  if (!readFrom(MAG, 0x03, b, 7)) return false;            // HXL..HZH + ST2 (ST2 read ends the sample)
  if (b[6] & 0x08) return false;                           // ST2 HOFL: magnetic overflow
  for (int k = 0; k < 3; k++) {
    int16_t r = (int16_t)((b[2 * k + 1] << 8) | b[2 * k]); // little-endian, unlike the MPU
    out[k] = r * asa[k] * 0.15f;                           // 0.15 uT per LSB in 16-bit mode
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN, 400000);                    // 400 kHz like ArduPilot

  scanBus("bypass OFF (0x0C is normally hidden here)");

  readFrom(ADDR, 0x75, &who, 1);                           // WHO_AM_I
  Serial.printf("\nWHO_AM_I = 0x%02X : ", who);
  if (who == 0x71 || who == 0x73)  Serial.println("MPU9250/9255 - should have a compass inside");
  else if (who == 0x70)            Serial.println("MPU6500 - IMU works, but there is NO compass inside");
  else                             Serial.println("NOT an ID ArduPilot's Invensense driver expects (clone?)");

  writeReg(0x6B, 0x80);                                    // PWR_MGMT_1: reset
  delay(100);
  writeReg(0x6B, 0x01);                                    // wake up, gyro clock
  delay(10);
  writeReg(0x1A, 0x03);                                    // CONFIG: gyro low-pass about 44 Hz
  writeReg(0x1B, 0x00);                                    // GYRO_CONFIG: +-250 dps = 131 per dps
  writeReg(0x1C, 0x00);                                    // ACCEL_CONFIG: +-2 g = 16384 per g
  writeReg(0x1D, 0x03);                                    // ACCEL_CONFIG2: accel low-pass about 44 Hz

  // --- open the door to the compass: I2C master off, bypass on (what ArduPilot does)
  writeReg(0x6A, 0x00);                                    // USER_CTRL: I2C_MST_EN = 0
  writeReg(0x37, 0x02);                                    // INT_PIN_CFG: BYPASS_EN = 1
  delay(10);
  uint8_t uc = 0xFF, ipc = 0xFF;
  readFrom(ADDR, 0x6A, &uc, 1);
  readFrom(ADDR, 0x37, &ipc, 1);
  Serial.printf("USER_CTRL = 0x%02X (expect 0x00), INT_PIN_CFG = 0x%02X (expect 0x02)\n", uc, ipc);

  scanBus("bypass ON (0x0C should appear now)");
  magOk = checkCompass();

  if (!magOk) {                                            // second try at 100 kHz
    Serial.println("Retrying the compass at 100 kHz...");
    Wire.setClock(100000);
    delay(10);
    magOk = checkCompass();
    Wire.setClock(400000);
  }

  Serial.println("\n=== RESULT ===");
  if (magOk) {
    Serial.println("Compass OK: it answers. Live compass values follow in the loop.");
    setupCompass();
  } else if (ipc != 0x02) {
    Serial.println("Bypass did not switch on (INT_PIN_CFG wrong): send this output.");
  } else {
    Serial.println("Bypass is ON but nothing answers at 0x0C: this module's compass is dead or missing.");
    Serial.println("Test it on the WEMOS build (COMPASS_DEV_ID) or use another GY-91.");
  }
  delay(200);
}

void loop() {
  double s[6] = {0}, q[6] = {0};
  double ms[3] = {0};
  int good = 0, magGood = 0;
  for (int i = 0; i < N; i++) {
    uint8_t b[14];
    if (readFrom(ADDR, 0x3B, b, 14)) {                     // accel x,y,z, temperature, gyro x,y,z
      int16_t r[7];
      for (int k = 0; k < 7; k++) r[k] = (int16_t)((b[2 * k] << 8) | b[2 * k + 1]);
      double v[6] = { r[0] / 16.384, r[1] / 16.384, r[2] / 16.384,     // accel in milli-g
                      r[4] / 131.0,  r[5] / 131.0,  r[6] / 131.0 };   // gyro in deg/s
      for (int k = 0; k < 6; k++) { s[k] += v[k]; q[k] += v[k] * v[k]; }
      good++;
    }
    float mag[3];
    if (magOk && readCompass(mag)) {
      for (int k = 0; k < 3; k++) ms[k] += mag[k];
      magGood++;
    }
    delay(5);
  }
  if (good < N / 2) {
    Serial.printf("Only %d/%d reads worked: check the wiring\n", good, N);
    delay(3000);
    return;
  }
  double m[6], sd[6];
  for (int k = 0; k < 6; k++) {
    m[k] = s[k] / good;
    sd[k] = sqrt(fmax(0.0, q[k] / good - m[k] * m[k]));
  }
  double accN = fmax(sd[0], sd[1]);
  double gyrN = fmax(fmax(sd[3], sd[4]), sd[5]);
  Serial.printf("\nreads OK %d/%d\n", good, N);
  Serial.printf("ACC  mean x %7.1f y %7.1f z %7.1f mg    noise x %5.2f y %5.2f z %5.2f mg\n",
                m[0], m[1], m[2], sd[0], sd[1], sd[2]);
  Serial.printf("GYRO mean x %6.2f y %6.2f z %6.2f dps   noise x %5.3f y %5.3f z %5.3f dps\n",
                m[3], m[4], m[5], sd[3], sd[4], sd[5]);
  Serial.printf("ACC noise %s   GYRO noise %s\n",
                accN <= 5 ? "GOOD" : accN <= 10 ? "OK" : "BAD",
                gyrN <= 0.15 ? "GOOD" : gyrN <= 0.3 ? "OK" : "BAD");
  if (magOk) {
    if (magGood > 0) {
      double mx = ms[0] / magGood, my = ms[1] / magGood, mz = ms[2] / magGood;
      double total = sqrt(mx * mx + my * my + mz * mz);
      Serial.printf("MAG  mean x %6.1f y %6.1f z %6.1f uT   |B| %5.1f uT (%d samples)\n",
                    mx, my, mz, total, magGood);
      Serial.println("     Turn the board slowly: x and y should change. |B| far above 100 uT = metal/magnet nearby");
    } else {
      Serial.println("MAG  answered at start but sends no data now: send this output");
    }
  } else {
    Serial.println("MAG  not available (see RESULT above)");
  }
  delay(2000);
}
```

**A good GY-91 prints:** `0x68` and `0x76` in the first scan; `WHO_AM_I = 0x71` (0x73 for an MPU9255); `INT_PIN_CFG = 0x02`; `0x0C` as well in the second scan; `Compass OK`; then `ACC noise GOOD   GYRO noise GOOD`.

The first GY-91 tested on this Super Mini read `WHO_AM_I` = 0x71 with quiet readings, but its compass did not answer. It has been replaced: the new module's compass and other sensors work, so this guide uses the compass exactly like your WEMOS. Run this check on any replacement module before you build.

| Result | Meaning |
| --- | --- |
| Serial Monitor shows nothing | **USB CDC On Boot** is Enabled (output goes to USB-C), wrong COM port, or RST not pressed after upload |
| Garbled text | Serial Monitor not at 115200 |
| `WHO_AM_I` is 0x70 | An MPU6500: no compass inside. Use a GY-91 with a genuine MPU9250 |
| `WHO_AM_I` not 0x70 / 0x71 / 0x73 | ArduPilot won't detect this chip. Use another module |
| `Bypass is ON but nothing answers at 0x0C` | This module's compass is dead or missing: use another GY-91 |
| Reads fail, or 0x68 / 0x76 missing from the scan | Wiring, pins that don't match the sketch, or the SDO/SAO pin (1.2) |
| BAD noise on the towel | The chip itself is noisy: replace the GY-91 |

**Check:** `WHO_AM_I` 0x71, 0x73 or 0x70, and GOOD (or at least OK) noise.

## Part 3: Build environment

**Already build the WEMOS in `~/ardupilot_esp32_build/ardupilot`?** Use the same folder and `get_idf` shortcut: do only 3.6 and 3.7, then go to Part 4. On a new PC, do 3.1–3.5 first.

Use **Ubuntu 22.04 or newer** (native, or WSL2 on Windows). Check with `lsb_release -a`.

**3.1 System packages**

```bash
sudo apt update

sudo apt install -y git wget flex bison gperf cmake ninja-build ccache \
  libffi-dev libssl-dev dfu-util libusb-1.0-0 python3 python3-pip python3-venv
```

**3.2 Clone ArduPilot**

```bash
mkdir -p ~/ardupilot_esp32_build && cd ~/ardupilot_esp32_build

git clone --recursive https://github.com/ArduPilot/ardupilot.git    # several GB

cd ardupilot

git checkout 9dd2e0f3                               # the commit your WEMOS was built from

git submodule update --init --recursive

git submodule status | grep -E '^[-+U]'             # must print nothing

Tools/environment_install/install-prereqs-ubuntu.sh -y
```

Then **close the terminal and open a new one**, so the new PATH loads.

**3.3 Fetch ESP-IDF 5.3**

```bash
cd ~/ardupilot_esp32_build/ardupilot

./Tools/scripts/esp32_get_idf.sh
```

**3.4 Create the ESP-IDF Python environment and compilers** (in a fresh terminal)

```bash
cd ~/ardupilot_esp32_build/ardupilot

./modules/esp_idf/install.sh esp32,esp32s3           # ends with "All done!"
```

**3.5 The `get_idf` shortcut.** Add this line to the end of `~/.bashrc`, then open a new terminal:

```bash
alias get_idf='source ~/ardupilot_esp32_build/ardupilot/modules/esp_idf/export.sh'
```

Then, once:

```bash
cd ~/ardupilot_esp32_build/ardupilot

get_idf

python3 -m pip install empy==3.3.4 pexpect
```

**3.6 Check the folder is ready for the S3.** Your folder holds the WEMOS edits, which stay. This makes sure nothing is left from the earlier S3 attempt (its USB and WiFi driver changes, and its old build output):

```bash
cd ~/ardupilot_esp32_build/ardupilot

git status --short

grep -rc "ESP32 FIX 7/8\|ESP32 FIX 9" libraries/AP_HAL_ESP32/*.cpp libraries/AP_HAL_ESP32/*.h | grep -v ":0"

rm -rf build/esp32s3lukas                            # old S3 build output only; the WEMOS build folder stays
```

**Check:**

- `git status --short` lists only your WEMOS edits (`ArduCopter/esc_calibration.cpp`, `ArduCopter/takeoff_check.cpp`, `libraries/AP_HAL/board/esp32.h`, `libraries/AP_HAL_ESP32/Scheduler.h`, `libraries/AP_Param/AP_Param.cpp`) plus `??` lines for your hwdef folders.
- The `grep` prints **nothing**.
- If the grep prints a file, or `git status` lists `UARTDriver`, `WiFiDriver`, `WiFiUdpDriver` or the S3 `sdkconfig.defaults`, put those back to stock before going on:

```bash
git checkout -- libraries/AP_HAL_ESP32/UARTDriver.cpp libraries/AP_HAL_ESP32/UARTDriver.h libraries/AP_HAL_ESP32/WiFiDriver.cpp libraries/AP_HAL_ESP32/WiFiDriver.h libraries/AP_HAL_ESP32/WiFiUdpDriver.cpp libraries/AP_HAL_ESP32/WiFiUdpDriver.h libraries/AP_HAL_ESP32/targets/esp32s3/esp-idf/sdkconfig.defaults
```

**3.7 Confirm the S3 compiler is installed**

```bash
get_idf

which xtensa-esp32s3-elf-gcc                         # must print a path under ~/.espressif/tools
```

**Check:** it prints a path. If it prints nothing, run `./modules/esp_idf/install.sh esp32,esp32s3` again: an earlier install may have fetched the classic ESP32 tools only.

## Part 4: Board files and the fixes

ArduPilot builds a board from two files in its own folder: `hwdef.dat` (pins and sensors) and `defaults.parm` (parameters baked into the firmware, applied to every parameter you haven't saved yet). **The board name must start with `esp32s3`**: that is how the build picks the S3 chip (`Tools/ardupilotwaf/boards.py`). Every step is safe to run again, and both files are written whole, so an old `esp32s3lukas` folder is simply overwritten.

### 4.1 Create the board folder and `hwdef.dat`

The sensor, compass, ADC and WiFi lines follow your WEMOS file; the pins, UART and SD-card lines are what the Super Mini needs. **SERIAL0 is UART0 on GPIO44 (RX) / GPIO43 (TX)**: the USB-TTL, exactly like the WEMOS's UART0 behind its USB chip.

```bash
cd ~/ardupilot_esp32_build/ardupilot

mkdir -p libraries/AP_HAL_ESP32/hwdef/esp32s3lukas

cat > libraries/AP_HAL_ESP32/hwdef/esp32s3lukas/hwdef.dat <<'EOF'
# esp32s3lukas: ESP32-S3 Super Mini (4 MB flash) + GY-91 on I2C + MTF-01, indoor build
# USB-TTL adapter on TX (GPIO43) / RX (GPIO44): flashing and Mission Planner, like the WEMOS's USB chip
# Header pins on the Super Mini: GPIO1-13, TX=43, RX=44. Avoid GPIO3 (strapping), 19/20 (USB), 48 (LED).
define HAL_ESP32_BOARD_NAME "esp32s3-lukas"

# --- I2C bus 0: SDA GPIO12, SCL GPIO13 (the wiring tested with the check sketch)
#             Port       SDA         SCL          SPEED    INTERNAL SOFT
ESP32_I2CBUS  I2C_NUM_0  GPIO_NUM_12 GPIO_NUM_13  400*KHZ  false    false

# --- IMU: the GY-91's MPU9250 at 0x68 (SDO/SAO low)
define HAL_INS_DEFAULT HAL_INS_MPU60XX_I2C
IMU Invensense I2C:0:0x68 ROTATION_NONE
define INS_MAX_INSTANCES 3

# --- Barometer: the GY-91's BMP280 at 0x76
BARO BMP280 I2C:0:0x76
define HAL_BARO_ALLOW_INIT_NO_BARO 1

# --- Compass: AK8963 inside the MPU9250 (found only if the module's compass works)
define AP_COMPASS_PROBING_ENABLED 1
COMPASS AK8963:probe_mpu9250 I2C:0:0x0C ROTATION_NONE
define AP_COMPASS_AK8963_ENABLED 1

# --- no airspeed sensor
define AP_AIRSPEED_ENABLED 0

# --- ADC (battery monitor, optional): ADC1 only, ADC2 is unusable with WiFi on
ESP32_ADC_PIN ADC1_GPIO1_CHANNEL 11   1
ESP32_ADC_PIN ADC1_GPIO2_CHANNEL 11   2

# --- WiFi access point (change SSID and password); Mission Planner connects by TCP to 192.168.4.1:5760
define HAL_ESP32_WIFI 1
define WIFI_SSID "lukas-drone-s3"
define WIFI_PWD "changeme123"

# --- motor outputs 1-4
ESP32_RCOUT GPIO_NUM_4
ESP32_RCOUT GPIO_NUM_5
ESP32_RCOUT GPIO_NUM_6
ESP32_RCOUT GPIO_NUM_7

# --- RC input (PPM) on GPIO8
define HAL_ESP32_RCIN GPIO_NUM_8
define HAL_ESP32_RMT_RX_PIN_NUMBER 8

# --- UARTs: UART0 on RX (44) / TX (43) = SERIAL0, the USB-TTL adapter (MAVLink + console);
#     UART1 on GPIO9 (RX) / GPIO10 (TX) = MTF-01 (SERIAL3)
#             PORT        RXPIN        TXPIN
ESP32_SERIAL  UART_NUM_0  GPIO_NUM_44  GPIO_NUM_43
ESP32_SERIAL  UART_NUM_1  GPIO_NUM_9   GPIO_NUM_10

# --- no SD card slot: SD and file system off (the SD driver would also claim GPIO2)
define AP_FILESYSTEM_ESP32_ENABLED 0
define HAL_LOGGING_FILESYSTEM_ENABLED 0
define HAL_LOGGING_DATAFLASH_ENABLED 0
define HAL_LOGGING_MAVLINK_ENABLED 1
define HAL_LOGGING_BACKENDS_DEFAULT 0

# --- optical flow (MTF-01): needs Fix 1
define AP_OPTICALFLOW_ENABLED 1
EOF
```

### 4.2 Create `defaults.parm`

This is **your WEMOS `defaults.parm`** with two comments changed: the first names this board, and the MTF-01 comment gives the S3 pins. Every value is the same, including `EK3_SRC1_YAW 1` (heading from the compass).

**One line added at the end:** `MAV1_PARAMS 20`. It makes the first parameter download over the USB-TTL faster after power-up (6.1). MAV1 is SERIAL0, the USB-TTL port. If you already set it to 20 in Mission Planner, nothing changes; after an `erase_flash` it comes back from this file.

```bash
cd ~/ardupilot_esp32_build/ardupilot

cat > libraries/AP_HAL_ESP32/hwdef/esp32s3lukas/defaults.parm <<'EOF'
# esp32s3lukas defaults (ESP32-S3 Super Mini + GY-91 + MTF-01, indoor). Applied to every never-saved parameter.

# --- loop rate: the ESP32 cannot run 400 Hz
SCHED_LOOP_RATE 150

# --- frame: quad X, plain PWM ESCs
FRAME_CLASS 1
FRAME_TYPE 1
MOT_PWM_TYPE 0
MOT_PWM_MIN 1000
MOT_PWM_MAX 2000

# --- flight modes on channel 5: Stabilize / AltHold / AltHold (Loiter stays OFF until Part 8)
FLTMODE_CH 5
FLTMODE1 0
FLTMODE2 0
FLTMODE3 0
FLTMODE4 2
FLTMODE5 2
FLTMODE6 2

# --- channel 6 = arm (up) / disarm (down); after boot flip it down, then up
RC6_OPTION 153

# --- never start ESC calibration from ArduPilot: it hangs the ESP32 (calibrate with the sketch)
ESC_CALIBRATION 9

# --- logging off: no SD card on the MINI32 (set 1 once you fit one)
LOG_BACKEND_TYPE 0

# --- failsafes
FS_THR_ENABLE 3
FS_THR_VALUE 975
FS_GCS_ENABLE 0
FS_EKF_ACTION 1

# --- no compass on either option (set COMPASS_ENABLE 1 and EK3_SRC1_YAW 1 if you add one)
COMPASS_ENABLE 0

# --- MTF-01 on SERIAL3 (GPIO9 RX / GPIO10 TX)
SERIAL3_PROTOCOL 1
SERIAL3_BAUD 115
FLOW_TYPE 5
RNGFND1_TYPE 10
RNGFND1_MIN 0.01
RNGFND1_MAX 8
RNGFND1_ORIENT 25

# --- EKF3 sources for INDOOR flight: flow velocity, rangefinder height, no yaw source
EK3_SRC_OPTIONS 0
EK3_SRC1_POSXY 0
EK3_SRC1_VELXY 5
EK3_SRC1_POSZ 2
EK3_SRC1_VELZ 0
EK3_SRC1_YAW 1

# --- faster first parameter download over the USB-TTL (SERIAL0 = MAV1)
MAV1_PARAMS 20
EOF
```

**Comments older than the build.** As in the WEMOS file, they are left as they are: with Fix 4, Mission Planner's **Calibrate ESCs** works too (Part 7), and the SD-card comment names the WEMOS board.

**The compass comments** say no compass is used, also as in the WEMOS file. The `COMPASS AK8963:probe_mpu9250` line in `hwdef.dat` is what lets the GY-91's compass work; you switch it on with `COMPASS_ENABLE 1` in 6.2. The EKF3 comment says "no yaw source", but the value is `EK3_SRC1_YAW 1`, compass heading, which is what the WEMOS flies with.

### 4.3 Fix 1: turn optical flow back on

ArduPilot's `libraries/AP_HAL/board/esp32.h` forces `AP_OPTICALFLOW_ENABLED 0` for every ESP32 board. This wraps it so your board's `define AP_OPTICALFLOW_ENABLED 1` wins. Already in your folder from the WEMOS build? The commands change nothing.

```bash
cd ~/ardupilot_esp32_build/ardupilot

F=libraries/AP_HAL/board/esp32.h

grep -q "#ifndef AP_OPTICALFLOW_ENABLED" $F || \
  sed -i 's/^#define AP_OPTICALFLOW_ENABLED 0$/#ifndef AP_OPTICALFLOW_ENABLED\n#define AP_OPTICALFLOW_ENABLED 0\n#endif/' $F

grep -n -B1 -A1 "AP_OPTICALFLOW_ENABLED" $F
```

**Check:** three lines in a row: `#ifndef AP_OPTICALFLOW_ENABLED`, `#define AP_OPTICALFLOW_ENABLED 0`, `#endif`.

### 4.4 Fix 2: motors stuck at idle

```bash
cd ~/ardupilot_esp32_build/ardupilot

python3 - <<'EOF'
p = "ArduCopter/takeoff_check.cpp"
s = open(p).read()
if "no ESC telemetry: nothing to check" in s:
    print("already fixed - nothing to do")
else:
    a = "#if HAL_WITH_ESC_TELEM && FRAME_CONFIG != HELI_FRAME"
    b = "        takeoff_check_state.warning_ms = now_ms;\n"
    c = "    const bool motor_check_passed = motors_takeoff_check(g2.takeoff_rpm_min, g2.takeoff_rpm_max);\n"
    assert s.count(a) == 1 and s.count(b) == 1 and s.count(c) == 1, "file differs from expected - stop and check"
    s = s.replace(a, "#if FRAME_CONFIG != HELI_FRAME")
    s = s.replace(b, "#if HAL_WITH_ESC_TELEM\n" + b + "#endif\n")
    s = s.replace(c, "#if HAL_WITH_ESC_TELEM\n" + c + "#else\n    // no ESC telemetry: nothing to check\n    const bool motor_check_passed = true;\n#endif\n")
    open(p, "w").write(s)
    print("fixed")
EOF
```

**Check:** it prints `fixed` or `already fixed - nothing to do`.

### 4.5 Fix 3: I2C IMU reader priority

```bash
cd ~/ardupilot_esp32_build/ardupilot

sed -i 's/I2C_PRIORITY = 5;/I2C_PRIORITY = 24;/' libraries/AP_HAL_ESP32/Scheduler.h

grep -n "I2C_PRIORITY = 24" libraries/AP_HAL_ESP32/Scheduler.h
```

**Check:** the grep prints exactly one line.

### 4.6 Fix 4: ESC calibration from Mission Planner

```bash
cd ~/ardupilot_esp32_build/ardupilot

python3 - <<'EOF'
p = "ArduCopter/esc_calibration.cpp"
s = open(p).read()
if "ESP32 FIX: parameter saving" in s:
    print("already fixed - nothing to do")
else:
    a = "    // clear esc flag for next time\n    g.esc_calibrate.set_and_save(ESCCAL_NONE);\n\n    if (motors->is_normal_pwm_type()) {\n"
    assert s.count(a) == 1, "file differs from expected - stop and check"
    b = ("    // clear esc flag for next time\n"
         "    g.esc_calibrate.set_and_save(ESCCAL_NONE);\n"
         "#if CONFIG_HAL_BOARD == HAL_BOARD_ESP32\n"
         "    // ESP32 FIX: parameter saving and flash writing only run after setup()\n"
         "    // ends, and calibration never returns, so store the cleared flag now\n"
         "    g.esc_calibrate.save_sync(true, false);\n"
         "    for (uint32_t i = 0; i < HAL_STORAGE_SIZE / 8 + 1; i++) {\n"
         "        hal.storage->_timer_tick();\n"
         "    }\n"
         "#endif\n"
         "\n    if (motors->is_normal_pwm_type()) {\n")
    open(p, "w").write(s.replace(a, b))
    print("fixed")
EOF
```

**Check:** it prints `fixed` or `already fixed - nothing to do`. `CONFIG_HAL_BOARD == HAL_BOARD_ESP32` is true for S3 boards too.

### 4.7 Fix 6: tell the S3 build the flash is 4 MB

ArduPilot's S3 settings say the flash is 8 MB. At boot ESP-IDF compares that with the real chip and stops if the chip is smaller (`Detected size(4096k) smaller than the size in the binary image header(8192k)`). This file is read only by ESP32-S3 boards, never by the WEMOS or a classic ESP32 DevKit.

```bash
cd ~/ardupilot_esp32_build/ardupilot

sed -i 's/^CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y$/CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y/' \
  libraries/AP_HAL_ESP32/targets/esp32s3/esp-idf/sdkconfig.defaults

grep -n "FLASHSIZE" libraries/AP_HAL_ESP32/targets/esp32s3/esp-idf/sdkconfig.defaults
```

**Check:** it prints `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`.

### 4.8 Check the parameter names

```bash
cd ~/ardupilot_esp32_build/ardupilot/Tools/autotest/param_metadata

python3 param_parse.py --vehicle ArduCopter --format json > /dev/null   # creates apm.pdef.json

cd ~/ardupilot_esp32_build/ardupilot

python3 - <<'EOF'
import json
d = json.load(open('Tools/autotest/param_metadata/apm.pdef.json'))
names = {k for grp in d.values() for k in grp}
bad = [l.split()[0] for l in open('libraries/AP_HAL_ESP32/hwdef/esp32s3lukas/defaults.parm')
       if l.split('#')[0].strip() and l.split()[0] not in names]
print("Unknown parameters:", bad if bad else "none")
EOF
```

**Check:** it prints `Unknown parameters: none`.

## Part 5: Build, check and flash

### 5.1 Build

```bash
cd ~/ardupilot_esp32_build/ardupilot

get_idf

./waf configure --board=esp32s3lukas

./waf copter                                  # 10-15 minutes the first time
```

**Check:** it ends with `'copter' finished successfully` and an `ardupilot.bin binary size ... (37%) free` line. If `waf` stops at `esp-idf_showinc` with `constraints ... doesn't exist`, run `export IDF_PYTHON_CHECK_CONSTRAINTS=no`, delete `build/esp32s3lukas/esp-idf_build`, and build again.

To build the WEMOS again later, just pick its board: `./waf configure --board=esp32lukas6050`, then `./waf copter`. Each board builds into its own `build/` folder.

### 5.2 Confirm the sensors and all fixes are inside

```bash
cd ~/ardupilot_esp32_build/ardupilot

grep -o "\.sda=GPIO_NUM_[0-9]*, \.scl=GPIO_NUM_[0-9]*" build/esp32s3lukas/hwdef.h   # I2C pins

grep -o "HAL_ESP32_RCOUT {[^}]*}" build/esp32s3lukas/hwdef.h                       # motor pins

grep "HAL_INS_PROBE1" build/esp32s3lukas/hwdef.h                                    # IMU

grep -o "AK8963::probe_mpu9250([^;]*" build/esp32s3lukas/hwdef.h                     # compass driver

xtensa-esp32s3-elf-nm -C build/esp32s3lukas/esp-idf_build/ardupilot.elf | grep -c AP_OpticalFlow_MAV   # Fix 1

xtensa-esp32s3-elf-nm -C -S build/esp32s3lukas/esp-idf_build/ardupilot.elf | grep " Copter::takeoff_check()"   # Fix 2

grep -c "I2C_PRIORITY = 24" libraries/AP_HAL_ESP32/Scheduler.h                     # Fix 3

grep -c "ESP32 FIX" ArduCopter/esc_calibration.cpp                                  # Fix 4

grep 'CONFIG_ESPTOOLPY_FLASHSIZE=' build/esp32s3lukas/esp-idf_build/sdkconfig        # Fix 6

grep 'CONFIG_ESP_CONSOLE_UART_NUM=' build/esp32s3lukas/esp-idf_build/sdkconfig      # console on UART0 (the USB-TTL)

grep "define HAL_ESP32_WIFI " build/esp32s3lukas/hwdef.h                            # WiFi mode

grep -c "defaults.parm" build/esp32s3lukas/ap_romfs_embedded.h                      # defaults embedded
```

**Check** (values from the test build of these exact files at `9dd2e0f3`):

| Line | Must show |
| --- | --- |
| I2C pins | `.sda=GPIO_NUM_12, .scl=GPIO_NUM_13` |
| Motor pins | `{ GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7 }` |
| `HAL_INS_PROBE1` | `AP_InertialSensor_Invensense::probe(*this,GET_I2C_DEVICE(0,0x68),ROTATION_NONE)` |
| Compass | `AK8963::probe_mpu9250(GET_I2C_DEVICE(0,0x0c),ROTATION_NONE)` |
| `AP_OpticalFlow_MAV` | 8. A 0 means 4.3 didn't run |
| `Copter::takeoff_check()` | Size `000000a3`. `00000005` means 4.4 didn't run |
| `I2C_PRIORITY = 24` | 1 |
| `ESP32 FIX` (ESC) | 1 |
| Flash size | `CONFIG_ESPTOOLPY_FLASHSIZE="4MB"`. `"8MB"`: do 4.7, delete `build/esp32s3lukas`, configure and build again |
| Console | `CONFIG_ESP_CONSOLE_UART_NUM=0` |
| WiFi mode | `#define HAL_ESP32_WIFI 1` (TCP) |
| `defaults.parm` | 1 |

### 5.3 Flash over the USB-TTL

The **first** flash of this board must erase the flash, so `defaults.parm` applies to every parameter and nothing from an old sketch is left. Later flashes skip `erase_flash`: your parameters and calibrations stay. Close Mission Planner, the Arduino Serial Monitor and any terminal on the adapter's port first: only one program can hold it.

**Step 1, download mode first:** unplug the Super Mini's power, **hold BOOT**, plug its USB-C power in, release BOOT (2.1).

**Step 2, on Windows with WSL2,** pass the adapter to Ubuntu. In an **Admin PowerShell**:

```powershell
usbipd list                                               # find the adapter (CH340 = 1a86:7523, CP2102 = 10c4:ea60); note its BUSID, e.g. 1-3

usbipd bind --busid 1-3                                   # once per adapter

usbipd attach --wsl --busid 1-3                           # every time you plug it in; keep Ubuntu open
```

The adapter stays connected when the S3 resets, so no `--auto-attach` is needed.

**Step 3, flash in Ubuntu:**

```bash
ls /dev/ttyUSB*                                           # the adapter, e.g. /dev/ttyUSB0

cd ~/ardupilot_esp32_build/ardupilot

get_idf

esptool.py --chip esp32s3 -p /dev/ttyUSB0 erase_flash       # first flash of this board only

ESPPORT=/dev/ttyUSB0 ESPBAUD=921600 ./waf copter --upload
```

These are the same commands as on the WEMOS. The only difference is the reset: esptool tries to reset the board through the adapter's DTR/RTS wires, which your adapter doesn't have, so nothing happens. That is harmless, because the board is already in download mode. When the upload ends with `Hard resetting via RTS pin...`, **press RST** yourself. If the upload fails partway at 921600 (common on CH340), use `ESPBAUD=460800`.

**Or flash from Windows directly** (after `pip install esptool` in Windows). Copy `bootloader/bootloader.bin`, `partition_table/partition-table.bin` and `ardupilot.bin` from `build/esp32s3lukas/esp-idf_build/` to one folder, put the S3 in download mode, then, with your adapter's COM port:

```powershell
python -m esptool --chip esp32s3 --port COM7 erase_flash

python -m esptool --chip esp32s3 --port COM7 --baud 921600 write_flash --flash_mode dio --flash_size 4MB 0x0 bootloader.bin 0x10000 partition-table.bin 0x20000 ardupilot.bin
```

**Check:** it ends with `Hash of data verified`. Then **press RST** to start ArduPilot. `Failed to connect`: the S3 wasn't in download mode, or TX/RX/GND (2.1). If only the second command fails right after the erase, enter download mode again and rerun it. WSL users: give the adapter back to Windows for Mission Planner with `usbipd detach --busid 1-3`.

### 5.4 Back up the S3 once it works

When the S3 is set up and working, copy its whole flash to your PC, as for the WEMOS. Download mode first, then:

```bash
esptool.py --chip esp32s3 -p /dev/ttyUSB0 --before no_reset --after no_reset read_flash 0 0x400000 s3_working_backup.bin
```

To put it back (download mode first):

```bash
esptool.py --chip esp32s3 -p /dev/ttyUSB0 --before no_reset --after no_reset -b 460800 write_flash 0x0 s3_working_backup.bin
```

### 5.5 Check that it boots

The adapter carries ArduPilot's boot log and then MAVLink on the same line, as the WEMOS's USB does. Open a serial monitor on the adapter at 115200 (Arduino IDE, or `python3 -m serial.tools.miniterm /dev/ttyUSB0 115200` in Ubuntu, quit with Ctrl+\]) and press **RST**.

| In the log | Meaning |
| --- | --- |
| `rst:0x1 (POWERON)` or `rst:0x15` at the top | Normal |
| `Init ArduCopter V4.8.0-dev (9dd2e0f3)` | ArduPilot started |
| `Firmware change: erasing EEPROM...` | Normal on the first boot after `erase_flash` only |
| `loop_rate: actual: ...Hz, expected: 150Hz` every 10 s | **Startup finished**, the main loop runs |
| Unreadable characters after that | Normal: that is MAVLink, binary, for Mission Planner |
| `Config Error: INS: unable to initialise driver` every 5 s | IMU not found: wiring or I2C pins (1.2) |
| `Detected size(4096k) smaller than the size in the binary image header(8192k)` | Fix 6 missing: do 4.7, rebuild |
| `waiting for download` and nothing else | The S3 started in download mode: BOOT was held. Press RST alone |
| `rst:` lines that keep repeating | Crash, watchdog or brownout: see Troubleshooting |

**The first boot after `erase_flash` is the slowest** (about a minute): ArduPilot saves every parameter for the first time. Don't press RST or reopen the port during that minute.

**Check:** the `loop_rate` line appears and the log stops resetting. Close the serial monitor before opening Mission Planner.

## Part 6: Mission Planner setup and calibration

Props **off** for this whole Part. Reboot the board (press RST) after any group of parameter changes. The adapter stays connected through a reboot, so Mission Planner reconnects as soon as ArduPilot is back up.

### 6.1 Connect

- **USB-TTL (best for setup):** pick the adapter's COM port (**USB-SERIAL CH340**, **Silicon Labs CP210x** or similar) at **115200** (not AUTO) and click **Connect**. In **Config → Planner**, **untick Reset on USB connect (DTR)**, as on the WEMOS.
- **WiFi (good for watching values during tests):** join the `lukas-drone-s3` network, then connect with **TCP**, host **192.168.4.1**, port **5760**, the same as the WEMOS.

**Check:** the HUD shows **DISARMED**, and the parameter list downloads. The first download after power-up is the slow one; with MAV1\_PARAMS 20 (4.2) it is clearly quicker than the minute it took without it. Reconnecting while the board stays powered is near instant.

### 6.2 Set the parameters that need a reboot first

In **Config → Full Parameter List**, set these and **Write Params**, then reboot. They are the same settings as on your WEMOS:

| Parameter | Value | Why |
| --- | --- | --- |
| `MAV4_OPTIONS` | 2 | Don't forward MAVLink to or from the MTF-01. **First check `MAV4_DEVID` = 66310** (SERIAL3), as on the WEMOS. The numbering is the same: SERIAL0 (USB-TTL) = MAV1, SERIAL1 (WiFi) = MAV2, SERIAL2 = MAV3 (no pins, but MAVLink by default), SERIAL3 (MTF-01) = MAV4. Leave `MAV3_OPTIONS` at 0 |
| `SERIAL3_OPTIONS` | 0 | Leave at 0: the old bit 1024 for "don't forward" blocks arming on 4.7+ firmware |
| `FLOW_ORIENT_YAW` | 0 | Starting value; 6.8 tells you whether to change it |
| `COMPASS_ENABLE` | 1 | Turns on the GY-91's AK8963 compass, as on the WEMOS. `EK3_SRC1_YAW` is already 1 (heading from the compass) |

Confirm `SCHED_LOOP_RATE` = 150, `FLOW_TYPE` = 5, `ESC_CALIBRATION` = 9, `EK3_SRC1_YAW` = 1 and `MAV1_PARAMS` = 20; if they aren't, the flash wasn't erased (5.3).

### 6.3 Were the sensors found?

| Parameter | Good |
| --- | --- |
| `INS_ACC_ID`, `INS_GYR_ID` | Any non-zero number: the IMU was found |
| `BARO1_DEVID` | **226817**: BMP280, address 0x76, I2C bus 0, the same number as on the WEMOS. 0 means no barometer |
| `COMPASS_DEV_ID` | **265217** = hex 0x040C01: AK8963, address 0x0C, I2C bus 0, the same number as on the WEMOS. 0 means no compass: check `COMPASS_ENABLE` = 1 and the Part 2 compass result |

The Messages tab after boot should also show `Barometer 1 calibration complete`.

### 6.4 Accelerometer and level

Always **power up with the drone sitting still**: ArduPilot measures the gyro's zero point in the first seconds.

In **Setup → Mandatory Hardware → Accel Calibration**: **Calibrate Accel** (six positions), then **Calibrate Level**. **Check:** the HUD is level when the frame is level, and there is no `DCM Roll/Pitch inconsistent` message.

### 6.5 Orientation and IMU still check

**Orientation.** Tilt the nose down: the HUD should pitch nose down. Lift the left side: the HUD should roll right. If either is wrong, set `AHRS_ORIENTATION` by where the GY-91's Y axis points: right = 2, backward = 4, left = 6. Reboot, check again, then redo 6.4.

**IMU still check** (Data → Status, frame on a level table, 5 minutes):

| Value | Good |
| --- | --- |
| HUD roll / pitch | Steady within about ±1° |
| `ax`, `ay` | Near 0, within about ±10 milli-g |
| `az` | Near −1000 (1 g) |
| HUD heading | Steady, no slow creep (the compass holds it) |

### 6.6 Barometer check

| Value (Data → Status) | Good |
| --- | --- |
| `press_abs` | About 1000–1015 hPa, changing only slightly at rest |
| Lift test: floor → above your head → floor | `press_abs` drops by about 0.2 hPa and comes back |

### 6.7 Radio, flight modes and arm switch

| Step | How | Check |
| --- | --- | --- |
| Radio | **Setup → Mandatory Hardware → Radio Calibration**: every stick to its corners, every switch through every position, then **Click when Done** | Throttle moves bar 3; roll, pitch, yaw on bars 1, 2, 4. No bars at all: check PPM on GPIO8 |
| Flight modes | **Setup → Flight Modes**: flip CH5 | The mode changes (Stabilize / AltHold) |
| Arm switch | **Data → Status → `ch6in`** while you flip CH6 | Below 1200 and above 1800; if not, set the radio's CH6 end points to 100% |

### 6.8 MTF-01 checks

Configure the MTF-01 in **MicoAssistant**: protocol `mav_apm`, `mav_id` 200, baud 115200, the same as on the WEMOS.

| Value (30–50 cm over a textured floor) | Good |
| --- | --- |
| `rangefinder1` | Follows the height (about 30 at 30 cm, 50 at 50 cm) |
| `opt_qua` | Well above 0 |
| Messages | No `PreArm: Rangefinder 1: No Data` |

**Tilt test:** hold the drone level about 40 cm over the floor and rock it left-right, then nose up-down, about ±20° once a second. `opt_m_x` and `opt_m_y` should stay small (under about 0.3).

| While rocking | Do |
| --- | --- |
| Both stay small | Correct: keep `FLOW_ORIENT_YAW 0` |
| Both swing large (about 1–3) | `FLOW_ORIENT_YAW = 18000`, reboot, repeat |
| Rolling moves `opt_m_y` instead | Try `9000`, then `-9000`; keep the one that passes |

Don't fly Loiter or PosHold until this passes. **Move test:** right-click the map → **Set EKF Origin Here**, carry the drone level at about 50 cm, 1 m forward, then 1 m right: the map icon should follow and stay put when you stop.

### 6.9 WiFi link check

The WiFi code is ArduPilot's stock code, the same as on the WEMOS. Use the USB-TTL for setup; WiFi is for watching values during tests. With Mission Planner on WiFi (TCP) from about 3 m away, it should stay connected for 5 minutes. If it drops:

1. **Stop Windows switching networks:** the drone's network has no internet, so untick **Connect automatically** on your other saved WiFi networks while flying.
2. **Turn off the laptop's WiFi power saving:** Device Manager → your WiFi adapter → Power Management → untick Allow the computer to turn off this device.
3. **Give the antenna room:** keep the Super Mini's antenna end clear of the battery, ESC leads and metal.
4. **Steady the power:** a 470 µF capacitor across 5V and GND near the board.

Change one thing at a time, so you know which one helped.

### 6.10 Compass calibration and heading check

1. **Setup → Mandatory Hardware → Compass.** The list should show the AK8963 (the `COMPASS_DEV_ID` from 6.3) with **Use Compass 1** ticked.
2. Take the drone **away from steel, metal-framed desks and the laptop**, click **Start** under Onboard Mag Calibration, and rotate the drone slowly through every axis until the bar completes.
3. When it reports success, **reboot**.
4. **Heading check:** compare the HUD heading with a phone compass, nose pointing N, E, S and W. **Check:** within about 15° at each.

If calibration keeps failing or the heading is off by 90° or 180°, recheck the orientation (6.5) first. Motor interference is measured with props on in 8.2.

## Part 7: ESC calibration

The same method as the WEMOS: with Fix 4, Mission Planner's **Calibrate ESCs** works; the sketch in 7.3 is the backup.

### 7.1 Get ready (props OFF)

1. **Hardware:** all **props off**. ESC signal wires on GPIO4, GPIO5, GPIO6 and GPIO7, their grounds on GND, and **one ESC's red BEC wire on the Super Mini's 5V pin**: the battery must power the board, because USB will be unplugged.
2. **Mission Planner** (over the USB-TTL): check `MOT_PWM_MIN` = 1000 and `MOT_PWM_MAX` = 2000.
3. **Setup → Mandatory Hardware → ESC Calibration → Calibrate ESCs.** This sets `ESC_CALIBRATION` to 3 for the next power-up, whatever it was before (your defaults have 9).

### 7.2 Calibrate (hardware only)

1. Unplug the **USB-TTL** from the PC and any **USB-C** cable, then the **battery**. Everything is off.
2. Plug in the **battery only**, touch nothing, and listen:

| Time after plugging in | You hear | Meaning |
| --- | --- | --- |
| About 1 s | ESC startup tune | ESCs powered |
| About 3–4 s | **beep-beep** | The S3 is sending full throttle; the ESCs store it as the maximum |
| About 5 s later | **3 short beeps, then 1 long beep** | Minimum stored: **calibrated** |

3. Unplug the battery.

**If the motors spin instead of beeping, unplug the battery at once** (that is why the props are off). Mission Planner shows nothing during calibration: on ESP32 MAVLink only starts after startup, and calibration happens inside startup.

**Check:** plug the USB-TTL back in and connect Mission Planner: `ESC_CALIBRATION` now reads **0**. Then **Setup → Optional Hardware → Motor Test** at 10%: every motor starts together with the others and spins smoothly.

### 7.3 Backup: calibrate with a sketch

Use this if Mission Planner's method doesn't work on your ESCs. Arduino IDE, **ESP32 core 3.x**, the same settings as 2.2: board **ESP32S3 Dev Module**, **USB CDC On Boot: Disabled**, **Upload Mode: UART0 / Hardware CDC**, the adapter's port. Upload in download mode (2.1), press RST, then open the Serial Monitor at 115200 with line ending **Newline**. The S3's PWM hardware allows at most 14-bit resolution, so this version uses 14 bits.

For this sketch only, keep the ESCs' red wires off the board: USB-C powers the Super Mini and the battery powers only the ESCs. Afterwards, flash ArduPilot back (5.3, without `erase_flash`).

```cpp
// GUIDED ESC CALIBRATION for 4 ESCs on an ESP32-S3 Super Mini - no ArduPilot. PROPS OFF.
// Serial goes out on TX/RX: use the USB-TTL. To start over: unplug the battery, press the board's RST button.

const int ESC_PINS[4] = {4, 5, 6, 7};      // motor 1-4 pins from the board file
const int BITS = 14;                       // the S3's PWM allows at most 14 bits

void setPulse(int us) {
  for (int i = 0; i < 4; i++) {
    ledcWrite(ESC_PINS[i], (uint32_t)us * ((1 << BITS) - 1) / 20000);   // 50 Hz, pulse width in us
  }
  Serial.printf("   [signal to ESCs = %d us]\n", us);
}

void waitEnter() {
  while (Serial.available()) Serial.read();               // clear old input
  while (!Serial.available()) delay(10);                  // wait for Enter
  while (Serial.available()) Serial.read();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  for (int i = 0; i < 4; i++) {
    if (!ledcAttach(ESC_PINS[i], 50, BITS)) Serial.printf("PWM setup FAILED on GPIO%d\n", ESC_PINS[i]);
  }
  setPulse(1000);                                         // safe start

  Serial.println("\n=========== ESC CALIBRATION ===========");
  Serial.println("STEP 1: Check ALL of these:");
  Serial.println("  - PROPS OFF");
  Serial.println("  - Battery UNPLUGGED");
  Serial.println("  - ESC signal wires on GPIO4, GPIO5, GPIO6, GPIO7");
  Serial.println("  - ESC black/brown signal ground wires on Super Mini GND");
  Serial.println("  - ESC red wires NOT connected (board runs from USB-C)");
  Serial.println(">> Press Enter when ready.");
  waitEnter();

  setPulse(2000);                                         // full throttle BEFORE the ESCs get power
  Serial.println("\nSTEP 2: Sending FULL throttle now.");
  Serial.println(">> PLUG IN THE BATTERY NOW.");
  Serial.println("   You will hear: startup tune, then 'beep-beep'.");
  Serial.println(">> Press Enter AS SOON AS you hear 'beep-beep' (within 2-3 seconds!).");
  waitEnter();

  setPulse(1000);                                         // minimum throttle -> ESC stores the range
  Serial.println("\nSTEP 3: Sending MINIMUM throttle now.");
  Serial.println("   You should hear: 3 beeps (3S battery), then 1 long beep = CALIBRATED.");
  Serial.println("   If you hear a repeating series of DIFFERENT tones instead, you were too slow:");
  Serial.println("   unplug the battery, press RST, and start again.");
  Serial.println(">> Press Enter after the long beep.");
  waitEnter();

  Serial.println("\nSTEP 4: Test. Motors WILL SPIN (props must be OFF).");
  Serial.println(">> Press Enter to start the test.");
  waitEnter();
  int steps[] = {1100, 1200, 1300, 1400, 1500};
  for (int i = 0; i < 5; i++) {
    setPulse(steps[i]);
    Serial.println("   all 4 motors should be spinning, and getting faster");
    delay(2000);
  }
  setPulse(1000);                                         // stop motors

  Serial.println("\nSTEP 5: DONE. Motors stopped.");
  Serial.println(">> UNPLUG THE BATTERY.");
  Serial.println("   The ESCs keep this calibration. You can flash ArduPilot back now.");
}

void loop() {
  // nothing - the signal stays at 1000 us (motors stopped)
}
```

### 7.4 ESC beep codes

| You hear | Meaning | Fix |
| --- | --- | --- |
| One beep about every 2 s | No signal | Signal wire on GPIO4–7; ESC signal ground to GND |
| Fast continuous beeping | Throttle not at minimum | Calibrate (7.2) |
| Double beeps repeating | Battery voltage out of range | Fully charged pack |

## Part 8: Bench test, then indoor flight in stages

### 8.1 Bench test (props OFF)

Work through every item; fix any failure before moving on.

- Boot log shows the `loop_rate` line and stops resetting (5.5)
- Mission Planner connects over the USB-TTL, and the parameter download is quick (6.1)
- `INS_ACC_ID` and `BARO1_DEVID` non-zero (6.3)
- HUD level and steady; orientation check passes (6.5)
- Radio bars correct; CH5 switches Stabilize / AltHold (6.7)
- Transmitter off → Mission Planner reports a radio failsafe
- MTF-01 data, tilt test and move test pass (6.8)
- WiFi link check passes (6.9)
- ESCs calibrated; **Motor Test** at 10%: A = front-right, B = rear-right, C = rear-left, D = front-left, each spinning the way the Part 1 motor table says (swap any two of a motor's three wires to reverse it)
- **Arm:** flip CH6 down, then up. HUD shows **ARMED**; `ch1out`–`ch4out` about **1100** with the stick down
- **Throttle:** `ch1out`–`ch4out` rise **together** toward about 1900
- **Stabilisation:** at about 30% throttle, tilt the frame by hand; the motors on the low side speed up
- **Disarm:** CH6 down stops the motors at once

| `ch1out`–`ch4out` with stick up | Meaning |
| --- | --- |
| Rise toward 1900 | Working |
| Stay at **1000** | Not armed: flip CH6 down then up, read the Arm:/PreArm: message |
| Stay at **1150** | Fix 2 (4.4) is missing |
| Split (two high, two low) | Redo Calibrate Level, check the GY-91 mount |

### 8.2 Props on

- **Unplug the USB-TTL wires from the drone for flight** (or tie them down well clear of the props): Mission Planner can follow over WiFi.
- Each prop matches its motor's spin direction, printed side up.
- **Tether** the drone for the first throttle-ups, with your finger on the CH6 disarm switch.
- Fly over a **textured floor** in good light, between about 0.3 and 2 m.
- **Compass/motor check:** with the drone tied down, run Setup → Optional Hardware → Compass/Motor Calib, raise the throttle slowly to about 50–75% and back, then finish. **Check:** interference under about 30%. Above that, move the battery and ESC power leads away from the GY-91 and repeat.

### 8.3 Fly in stages

| Stage | Mode | Pass when | Then |
| --- | --- | --- | --- |
| 1 | **Stabilize**, tethered, 0.5–1 m | Hovers without fighting you; `vibex/y/z` below about 15–20 and `clip0` not rising | Hover 1–2 minutes so `MOT_THST_HOVER` learns the hover throttle |
| 2 | **AltHold**, throttle stick at **mid** | Holds height with the stick centred | If it pumps up and down, check `vibez` and `rangefinder1` |
| 3 | **Loiter** or **PosHold** | Holds position hands-off | Only after 6.8 passed |

**Stage 3 procedure:** set `FLTMODE6 5` (Loiter) or `16` (PosHold), reboot, take off in **AltHold** to about 1 m, then switch with your finger on the mode switch. If it leans, accelerates or slowly turns its nose, switch straight back to AltHold and land: a wandering heading points to the compass (6.10, and the motor check in 8.2).

**Logs:** `LOG_BACKEND_TYPE 0` means nothing is recorded. This board has no SD card; to log, use MAVLink (`LOG_BACKEND_TYPE 2`, needs a log receiver connected).

## Troubleshooting

Find your symptom, do the fix, test again. One change at a time, otherwise you won't know which one helped.

**USB-TTL, flashing and boot**

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `Failed to connect to ESP32-S3` when flashing | Not in download mode, TX/RX not crossed, no GND, or adapter at 5 V | Hold BOOT, tap RST, release BOOT (2.1); adapter TX → GPIO44, RX → GPIO43, GND → GND; jumper on 3.3 V |
| The adapter has no COM port in Windows | Driver missing | Install the CH340 or CP210x driver; try another USB cable (some are charge-only) |
| Flashing works, but nothing runs afterwards | Still in download mode | Press RST alone (5.5: `waiting for download` means BOOT was held) |
| Boot loop with `Detected size(4096k) smaller than the size in the binary image header(8192k)` | Firmware built for 8 MB flash | Fix 6 (4.7), delete `build/esp32s3lukas`, configure and build again, flash |
| Mission Planner opens the port but gets no heartbeat | Wrong COM port or baud, a serial monitor still has the port, or startup hasn't finished | Adapter's port at 115200; close other programs; watch the boot log (5.5) |
| No heartbeat on the USB-TTL or WiFi; the boot log stops before `loop_rate` | IMU not found, so ArduPilot never finishes startup, or the slow first boot | Check SDA → GPIO12, SCL → GPIO13 and run the 2.2 sketch; after `erase_flash` wait a minute |
| `Config Error: INS: unable to initialise driver` | MPU9250 not answering on I2C | Same wiring check; the 2.2 sketch must show `WHO_AM_I` 0x71 |
| Arduino sketch uploads but the Serial Monitor shows nothing | **USB CDC On Boot** is Enabled, so `Serial` goes to USB-C; or RST not pressed | Set it to Disabled (2.2), upload again, press RST |
| `PWM setup FAILED` in your own sketch | The S3's PWM timer allows at most 14 bits | Use the 14-bit sketch (7.3). ArduPilot itself is not affected |

**Sensors, motors and flight**

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Parameters load slowly; the HUD lags | Fix 3 missing, or MTF-01 data forwarded to Mission Planner | Check 5.2 (`I2C_PRIORITY = 24`); `MAV4_OPTIONS 2` after checking `MAV4_DEVID` 66310 (6.2) |
| First "Getting Params" after power-up takes about a minute | `MAV1_PARAMS` is 0, so the one-by-one download runs at about 50 parameters a second | Check `MAV1_PARAMS = 20` (4.2, 6.2). Still slow: Cancel the download, then Config → Full Parameter List → Refresh Params, which uses the fast MAVFTP method |
| Rangefinder shows No Data | MTF-01 wiring or settings | MTF-01 TX → GPIO9, RX → GPIO10, 5V, GND. `SERIAL3_PROTOCOL 1`, `SERIAL3_BAUD 115`, `SERIAL3_OPTIONS 0`; MicoAssistant set to `mav_apm` |
| `FLOW_TYPE` missing in the parameter list | Optical flow not compiled in | Fix 1 (4.3), check `AP_OpticalFlow_MAV` = 8 in 5.2 |
| Outputs stuck at 1150 after arming | Motors-stuck-at-idle bug | Fix 2 (4.4), check the `takeoff_check` size in 5.2 |
| Outputs sit at 1000 | Not armed | Flip CH6 down then up; read the PreArm message |
| ESC calibration repeats at every power-up; Mission Planner never connects | `ESC_CALIBRATION` flag stuck in flash (firmware without Fix 4) | Erase only the parameter area (command below), flash firmware with Fix 4, redo Part 6 |
| Board resets when the motors spin up | Brownout on the 5 V rail | Power the S3 from a proper 5 V BEC; common ground between ESCs, BEC and board; short, thick power wires; 470 µF across 5V and GND |
| WiFi connects, then drops | Weak signal, power, or the laptop switching networks | The 6.9 checklist; use the USB-TTL for setup |
| `COMPASS_DEV_ID` is 0 | `COMPASS_ENABLE` is 0, the chip is an MPU6500 (no compass), or the module's compass is dead | Set `COMPASS_ENABLE` 1 (6.2); Part 2: WHO\_AM\_I 0x71/0x73 and the compass must answer at 0x0C |
| `PreArm: Compass not calibrated` | Compass calibration not done or not saved | Do 6.10 and reboot |
| Heading off by 90° or 180°, or compass calibration keeps failing | GY-91 turned on the frame, or steel nearby | Orientation check (6.5); calibrate away from steel |
| Heading wanders when the motors spin; Loiter/PosHold slowly turns or drifts | Motor current near the compass | Compass/motor check (8.2); move the power leads away from the GY-91 |
| Loiter leans hard or flips | Flow orientation inverted | Back to AltHold; redo the tilt test (6.8); fix `FLOW_ORIENT_YAW` |
| `waf` fails with `constraints ... doesn't exist` | ESP-IDF Python check | Commands below, then build again |

Clear a stuck ESC calibration (download mode first; this erases only the parameter storage, so `defaults.parm` applies again and Part 6 must be redone):

```bash
esptool.py --chip esp32s3 -p /dev/ttyUSB0 --before no_reset --after no_reset erase_region 0x320000 0x40000
```

Build constraints error:

```bash
export IDF_PYTHON_CHECK_CONSTRAINTS=no

rm -rf build/esp32s3lukas/esp-idf_build

./waf copter
```

## Sources

**Board and chip**

- [ESP32-S3 Super Mini pinout (TeachMeMicro)](https://www.teachmemicro.com/?p=12625)
- [ESP32-S3 Super Mini pinout and strapping pins (espboards.dev)](https://espboards.dev/esp32/esp32-s3-super-mini/)

**ArduPilot documentation and pull requests**

- [AP\_HAL\_ESP32 README](https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_HAL_ESP32/README.md)
- [MicoAir MTF-01](https://ardupilot.org/copter/docs/common-mtf-01.html): `MAVx_OPTIONS` = 2 on the sensor's port; MAVx counts MAVLink ports, not SERIAL numbers
- [Optical flow sensor setup](https://ardupilot.org/copter/docs/common-optical-flow-sensor-setup.html)
- [ESC calibration](https://ardupilot.org/copter/docs/esc-calibration.html)
- [Advanced compass setup](https://ardupilot.org/copter/docs/common-compass-setup-advanced.html)
- [ArduPilot PR #34070](https://github.com/ArduPilot/ardupilot/pull/34070) and [PR #34132](https://github.com/ArduPilot/ardupilot/pull/34132): the motors-stuck-at-idle bug

**Forum threads**

- [ESP32-S3 can't connect to Mission Planner](https://discuss.ardupilot.org/t/esp32-s3-cant-connect-to-mission-planner/141344?page=2)
- [450mm ESP32 Quadcopter Success](https://discuss.ardupilot.org/t/450mm-esp32-quadcopter-success/142049): a GY-91 on I2C flying on an ESP32

**ArduPilot source files checked** (commit `9dd2e0f3`)

- `libraries/AP_HAL_ESP32/hwdef/esp32s3devkit/hwdef.dat`: ArduPilot's own S3 board puts SERIAL0 on UART0, GPIO44 (RX) / GPIO43 (TX), the same pins this guide uses for the USB-TTL
- `libraries/AP_HAL_ESP32/targets/esp32s3/esp-idf/sdkconfig.defaults`: the S3 flash-size setting (Fix 6)
- `Tools/ardupilotwaf/boards.py`: a board name starting with `esp32s3` selects the S3 chip
- `ArduCopter/takeoff_check.cpp`, `libraries/AP_HAL/board/esp32.h`, `libraries/AP_HAL_ESP32/Scheduler.h`, `ArduCopter/esc_calibration.cpp`: Fixes 2, 1, 3 and 4
- `libraries/AP_SerialManager/AP_SerialManager.cpp` (`find_protocol_instance`): how MAVn channels are numbered
- The test build's partition table: parameter storage at `0x320000`, 256 KB (the erase command in Troubleshooting)
- `libraries/GCS_MAVLink/GCS_MAVLink_Parameters.cpp` and `GCS.cpp`: the stream-rate parameters are named MAVn\_ on this firmware (the old SRn\_ names are gone), so MAV1\_PARAMS is SERIAL0's parameter-sending rate

**Datasheet**

- InvenSense MPU-9250 Product Specification: AK8963 WIA = 0x48
