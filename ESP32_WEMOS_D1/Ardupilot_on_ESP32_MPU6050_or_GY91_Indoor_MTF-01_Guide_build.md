# ArduPilot on ESP32 + MPU6050 or GY-91 — Indoor MTF-01 Build Guide

Oct 12, 2026 · @lukas

This guide builds an indoor ArduCopter flight controller from a WEMOS D1 Mini ESP32 (MINI32), a MicoAir MTF-01 optical-flow and range sensor, and one of two IMU options on I2C: A, an MPU6050 (GY-521), or B, a GY-91, which adds a BMP280 barometer. One firmware runs both. It keeps every fix found on the earlier GY-91 (SPI) build, and adds a noise test so you can prove the IMU is good before it flies.

## At a glance

> **Status and safety.** ArduPilot on ESP32 is experimental. Keep props **off** until Part 8, fly tethered first, and never near people. Tested with ArduPilot master (`ArduCopter V4.8.0-dev`, commit `81ff4d6a`, 6 October 2026) and ESP-IDF 5.3: the `esp32lukas6050` board in this guide was built from that source, and every parameter in its `defaults.parm` was checked against ArduCopter's parameter list. It has not been flown on hardware yet; Part 2 and Part 8 are how you prove it.

**How to use this guide:** work through the Parts in order. Each step ends with a **Check**; don't move on until it passes. In command blocks, a blank line separates commands, a line ending in `\` continues on the next line, and text after `#` is a comment.

**What changes from the GY-91 build:**

|  | GY-91 build (`esp32lukas`) | This build (`esp32lukas6050`) |
| --- | --- | --- |
| IMU | MPU9250 on SPI (IO18/19/23, CS IO5) | Option A: **MPU6050**. Option B: **GY-91** (MPU9250). Both on I2C (SDA IO21, SCL IO22), address 0x68 |
| Barometer | BMP280 inside the GY-91 (SPI, CS IO26) | Option B: the GY-91's BMP280 on the same I2C bus (0x76). Option A: none, or an add-on BMP280 module |
| Compass | AK8963 inside the GY-91 | **None** on either option (`COMPASS_ENABLE 0`, `EK3_SRC1_YAW 0`); the GY-91's AK8963 stays unused. An external I2C compass is optional |
| I2C pins | IO13/IO12 (unused, because the GY-91 ran on SPI) | **IO21/IO22**. IO12 is a boot pin and would stop the ESP32 booting with an I2C pull-up on it |
| Motor outputs | IO25, 27, 33, 32 + IO22, 21 spare | IO25, 27, 33, 32 (IO21/22 now belong to I2C) |
| Everything else | MTF-01 on SERIAL3, PPM on IO4, WiFi, `SCHED_LOOP_RATE 150` | Same |

**Fixes carried over from the GY-91 build** (all needed, all in Part 4):

1. **Optical flow is switched off for every ESP32 board** in ArduPilot's `esp32.h`. Without the fix, `FLOW_TYPE` doesn't exist.
2. **Motors stuck at idle.** From ArduPilot 4.7 the motor library locks the motors at ground idle after arming, and on ESP32 nothing releases the lock: the drone arms but the outputs sit at 1150 and the throttle does nothing (upstream PR #34070 / #34132).
3. **ESC calibration from Mission Planner hangs the ESP32** at every power-up. This guide never uses it: `ESC_CALIBRATION 9` plus a calibration sketch (Part 7).

**Why swap the IMU at all?** On the GY-91 build the accelerometer jittered by about ±20 milli-g while sitting still, and the HUD rocked left and right. The same ArduPilot code on a Matek F405 sat still. Part 2 measures your MPU6050 (and your GY-91, with the same sketch) against the chip's datasheet, so you know whether the sensor or the setup is at fault before you fly. Option B keeps the GY-91 for its barometer, wired on I2C the way other flying ESP32 builds use it. I2C doesn't make a noisy chip quiet, so Part 2 still decides whether your GY-91 is good enough.

## Part 1: Parts and wiring

### 1.1 Pick your IMU option

Both IMUs use the same four wires to the ESP32 and run the same firmware: ArduPilot finds whichever chip answers at I2C address 0x68, and the barometer if one answers at 0x76. Pick by what you have, and run Part 2 on it first either way.

|  | Option A: MPU6050 (GY-521) | Option B: GY-91 |
| --- | --- | --- |
| IMU chip | MPU6050 (WHO\_AM\_I 0x68) | MPU9250 (0x71); some boards carry an MPU6500 (0x70) |
| Barometer | None (an add-on BMP280 module is optional) | BMP280 on the board, address 0x76 |
| If the MTF-01 rangefinder drops out | Nothing to fall back on: the height estimate drifts | The EKF switches to the barometer automatically |
| Wires to the ESP32 | VCC, GND, SDA, SCL | VIN, GND, SDA, SCL |
| Firmware | `esp32lukas6050` | The same `esp32lukas6050` |
| Parameters | `defaults.parm` as it is | `defaults.parm` as it is |

**What the barometer does here.** The EKF takes its height from the MTF-01's rangefinder (`EK3_SRC1_POSZ 2`). If the rangefinder stops sending for more than half a second (a shiny or black floor, too high, a loose wire), the EKF switches to the barometer, and back again when the rangefinder returns (`AP_NavEKF3_PosVelFusion.cpp`). With no barometer there is nothing to switch to. ArduPilot's optical-flow page uses the barometer as the main height source (`EK3_SRC1_POSZ 1`); this guide keeps the rangefinder as the main source for low indoor flight, because prop wash, doors and air-con push a barometer's reading around while the rangefinder measures the floor directly. So option B needs no parameter change: the barometer is the backup.

### 1.2 Parts

| Part | Notes |
| --- | --- |
| WEMOS D1 Mini ESP32 (MINI32) | Classic ESP32 (ESP32-WROOM-32, 4 MB flash). A classic ESP32 DevKit works with the same pins |
| IMU: MPU6050 module (GY-521) or GY-91 (see 1.1) | Both I2C, address 0x68, each with its own 3.3V regulator and I2C pull-up resistors |
| BMP280 module (option A only, optional) | I2C, address 0x76. The GY-91 already has one. Many modules are 3.3V-only. Without a barometer, height comes from the MTF-01 only |
| MicoAir MTF-01 | Optical flow + rangefinder, MAVLink on a UART |
| FlySky FS-A8S receiver + transmitter | PPM output |
| 4 × brushless ESC (40A, 5V BEC) + A2212 1400KV + 8045 props | Plain PWM ESCs; calibrated with the sketch in Part 7 |
| 3S LiPo | 11.1V nominal |
| Short jumper wires, soft foam tape | Keep the IMU wires under about 10 cm |

### 1.3 Pin tables

**Both options:**

| Device pin | MINI32 pin | Notes |
| --- | --- | --- |
| FS-A8S PPM | IO4 | Use the PPM output |
| FS-A8S + / − | 5V / GND |  |
| MTF-01 TX | IO16 | MINI32 RX, ArduPilot SERIAL3 |
| MTF-01 RX | IO17 | MINI32 TX |
| MTF-01 VCC / GND | 5V / GND |  |
| Motor 1–4 signal | IO25, IO27, IO33, IO32 | ESC signal wires |
| ESC signal grounds | GND | All four, for a clean PWM signal |
| 5V supply | 5V / GND | One ESC's BEC (red + black). Leave the other three ESCs' red wires unconnected |

**Option A: MPU6050 (GY-521)**

| MPU6050 pin | MINI32 pin | Notes |
| --- | --- | --- |
| VCC / GND | 5V / GND | The GY-521's own regulator makes 3.3V, and its pull-ups go to that 3.3V |
| SDA | IO21 | I2C data |
| SCL | IO22 | I2C clock |
| AD0 | GND (or unconnected) | Low = address 0x68 |
| INT, XDA, XCL | not connected |  |

An add-on BMP280 module for option A goes on the same bus: SDA / SCL to IO21 / IO22, VCC to 3V3 (unless the module has its own regulator), GND to GND, SDO to GND (address 0x76), CSB to 3V3 (I2C mode).

**Option B: GY-91 (MPU9250 + BMP280)**

| GY-91 pin | MINI32 pin | Notes |
| --- | --- | --- |
| VIN / GND | 5V / GND | VIN feeds the GY-91's own 3.3V regulator |
| 3V3 | not connected | The regulator's output: don't feed power into it |
| SDA | IO21 | I2C data, shared by the MPU9250 and the BMP280 |
| SCL | IO22 | I2C clock |
| SDO/SAO | GND (or unconnected) | Low = MPU9250 at 0x68 and BMP280 at 0x76. If the Part 2 scan shows 0x69 or 0x77, tie it to GND |
| NCS, CSB | not connected | High = I2C mode, normally pulled up on the board. If the scan misses 0x68 or 0x76, tie both to the GY-91's 3V3 pin |

The barometer measures air pressure, so keep moving air off it: cover the BMP280 (the small metal can with a pin-hole, next to the larger MPU9250 chip) with a piece of open-cell foam. Don't seal it airtight.

### 1.4 Wiring schematic

&#91;embedded content: Wiring schematic · both IMU options, MTF-01, receiver, 4 ESCs, power\]

Options A and B use the same four IMU wires, so the drawing shows one IMU box. Arrows show which way a signal travels; SDA and SCL carry data both ways. Every device's GND must join the same ground as the ESP32, or the signals have no reference.

### 1.5 Wiring rules

- **Never put I2C (or anything with a pull-up) on IO12.** IO12 is an ESP32 boot pin: pulled high at power-up, it sets the flash to 1.8V and the board won't boot. Also leave IO6–IO11 alone (internal flash).
- **Keep the IMU wires short** (under about 10 cm) and route them away from the ESC and battery wires. Twist SDA and SCL each with a GND wire if they must be longer.
- **USB and the BEC together:** the MINI32's 5V pin connects straight to USB 5V. During bench setup, unplug the BEC's red wire while USB is connected, or power the board from USB only.
- **Mount the IMU so ArduPilot reads it the right way.** ArduPilot's driver maps the chip's Y axis to the drone's forward direction (`AP_InertialSensor_Invensense.cpp`, lines 613–615). With `ROTATION_NONE`: **chip side up, the board's printed Y arrow pointing forward, the X arrow pointing right**. Mount it flat, near the centre of the frame, on firm foam that doesn't let it rock. If your board has no axis arrows, or ends up turned 90° or 180°, the orientation check in 6.4 shows it and AHRS\_ORIENTATION fixes it.
- **Mount the MTF-01** underneath, lens straight down, square to the frame, with a clear view of the floor (its camera sees a 42° cone). Part 6 checks its orientation.

**Motor layout** (quad X, viewed from above, front at the top). ArduPilot's numbering is not clockwise:

| Pin | Motor | Position | Spin | Motor Test button |
| --- | --- | --- | --- | --- |
| IO25 | 1 | front-right | CCW | A |
| IO27 | 2 | rear-left | CCW | C |
| IO33 | 3 | front-left | CW | D |
| IO32 | 4 | rear-right | CW | B |

**Check:** before powering up, confirm with a multimeter that 5V and GND aren't shorted, and that nothing is wired to IO12.

## Part 2: Test the IMU before you build (MPU6050 and GY-91)

This sketch scans the I2C bus, checks the chip ID ArduPilot needs, and measures how much the readings shake while the board sits still. Run it on the MPU6050, then on the GY-91 wired in I2C mode: the same test on both tells you whether the GY-91 itself was the problem.

Arduino IDE, ESP32 board package, board **ESP32 Dev Module**, Serial Monitor at 115200. Put the board on a folded towel, away from the laptop, and don't touch it while it measures.

```cpp
// IMU check for ArduPilot on ESP32: I2C scan, chip ID, and a still-noise test.
// Works for the MPU6050 (GY-521) and for the GY-91's MPU9250 wired in I2C mode.
// Wiring: SDA = IO21, SCL = IO22. Board on a folded towel, don't touch it.
#include <Wire.h>

const uint8_t ADDR = 0x68;          // AD0 / SDO low
const int N = 500;                  // samples per test (about 2.5 s)
uint8_t who = 0;

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;     // repeated start
  if (Wire.requestFrom(ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(21, 22, 400000);                              // SDA, SCL, 400 kHz like ArduPilot

  Serial.println("\n=== I2C scan ===");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("  device at 0x%02X\n", a);
  }

  readRegs(0x75, &who, 1);                                 // WHO_AM_I
  Serial.printf("WHO_AM_I = 0x%02X : ", who);
  if (who == 0x68)                     Serial.println("MPU6050/MPU6000 - ArduPilot recognises it");
  else if (who == 0x70)                Serial.println("MPU6500 - ArduPilot recognises it");
  else if (who == 0x71 || who == 0x73) Serial.println("MPU9250/9255 - ArduPilot recognises it");
  else                                 Serial.println("NOT an ID ArduPilot's Invensense driver expects (clone?)");

  writeReg(0x6B, 0x80);                                    // PWR_MGMT_1: reset
  delay(100);
  writeReg(0x6B, 0x01);                                    // wake up, gyro clock
  writeReg(0x1A, 0x03);                                    // CONFIG: low-pass about 44 Hz
  writeReg(0x1B, 0x00);                                    // GYRO_CONFIG: +-250 dps = 131 per dps
  writeReg(0x1C, 0x00);                                    // ACCEL_CONFIG: +-2 g = 16384 per g
  if (who != 0x68) writeReg(0x1D, 0x03);                   // MPU6500/9250 only: accel low-pass about 44 Hz
  delay(200);
}

void loop() {
  double s[6] = {0}, q[6] = {0};
  int good = 0;
  for (int i = 0; i < N; i++) {
    uint8_t b[14];
    if (readRegs(0x3B, b, 14)) {                           // accel x,y,z, temperature, gyro x,y,z
      int16_t r[7];
      for (int k = 0; k < 7; k++) r[k] = (int16_t)((b[2 * k] << 8) | b[2 * k + 1]);
      double v[6] = { r[0] / 16.384, r[1] / 16.384, r[2] / 16.384,     // accel in milli-g
                      r[4] / 131.0,  r[5] / 131.0,  r[6] / 131.0 };   // gyro in deg/s
      for (int k = 0; k < 6; k++) { s[k] += v[k]; q[k] += v[k] * v[k]; }
      good++;
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
  delay(2000);
}
```

**Wiring for the test:**

|  | MPU6050 (GY-521) | GY-91 in I2C mode |
| --- | --- | --- |
| SDA / SCL | IO21 / IO22 | SDA / SCL to IO21 / IO22 |
| Power | VCC to 5V, GND | VIN to 5V (or 3V3), GND; leave its 3V3 pin unconnected |
| Address pin | AD0 to GND | SDO/SAO to GND |
| Mode pins | — | NCS and CSB unconnected (pulled high on the board = I2C mode). If the scan misses 0x68 or 0x76, tie both to the GY-91's 3V3 pin |

A good MPU6050 prints something like `device at 0x68` (plus `0x76` if the BMP280 is fitted), `WHO_AM_I = 0x68`, and `ACC noise GOOD   GYRO noise GOOD`. The GY-91 shows `0x68` and `0x76` (its BMP280), and `WHO_AM_I = 0x71` (0x73 for an MPU9255; 0x70 means the board carries an MPU6500, which ArduPilot runs the same way, minus a compass this build doesn't use).

**What the noise numbers should be.** With the 44 Hz low-pass the sketch sets, the datasheets' typical noise works out to about **3 mg and 0.04 °/s** for the MPU6050 (400 µg/√Hz, 0.005 °/s/√Hz) and about **2.5 mg and 0.08 °/s** for the MPU9250 (300 µg/√Hz, 0.01 °/s/√Hz). The sketch calls GOOD up to about twice that. For comparison, the GY-91 in ArduPilot jittered by about ±20 mg; a healthy chip should stay within about ±10 mg (three times the noise figure).

**Run it three times, then read the result:**

1. On the towel, USB only.
2. Fitted on the frame, USB only.
3. On the frame with the battery plugged in (ESCs powered, props off, motors stopped).

| Result | Meaning |
| --- | --- |
| `WHO_AM_I` not 0x68 / 0x70 / 0x71 / 0x73 | ArduPilot won't detect this chip as wired. Use another module |
| Reads fail, or devices missing from the scan | Wiring, pull-ups, or a dead module |
| BAD on the towel | **The chip itself is noisy** (clone or damaged): replace it |
| GOOD on the towel, BAD on the frame | The frame or mount shakes it: firmer foam, no wires tugging, away from fans |
| GOOD until the battery goes in | Electrical noise from the ESCs or BEC: separate the IMU wires from power wires, add a capacitor across 5V |
| GOOD in all three | **The IMU is fine.** Any wobble left in ArduPilot is setup (mount, startup, EKF sources), not the sensor |

**Check:** your MPU6050 reads GOOD (or at least OK) in all three runs before you go on. If your GY-91 reads BAD where the MPU6050 reads GOOD, that confirms the GY-91 was the cause of the wobble.

## Part 3: Build environment (once)

**Already built the GY-91 firmware on this PC?** Skip to Part 4: the same `~/ardupilot_esp32_build/ardupilot` checkout and `get_idf` shortcut build this board too.

Use **Ubuntu 22.04 or newer** (native, or WSL2 on Windows). Check with `lsb_release -a`. Run these in order:

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

git submodule status | grep -E '^[-+U]'             # must print nothing

Tools/environment_install/install-prereqs-ubuntu.sh -y
```

Then **close the terminal and open a new one**, so the new PATH loads.

**3.3 Fetch ESP-IDF 5.3**

```bash
cd ~/ardupilot_esp32_build/ardupilot

./Tools/scripts/esp32_get_idf.sh

git -C modules/esp_idf describe --tags               # must show v5.3.x

git -C modules/esp_idf submodule status --recursive | grep -E '^[-+U]'   # must print nothing
```

**3.4 Create the ESP-IDF Python environment** (in a fresh terminal)

```bash
which python3                                        # must be /usr/bin/python3

echo "VIRTUAL_ENV=$VIRTUAL_ENV"                      # must be empty

cd ~/ardupilot_esp32_build/ardupilot

./modules/esp_idf/install.sh                         # ends with "All done!"
```

**3.5 The `get_idf` shortcut.** Add this one line to the end of `~/.bashrc`, then open a new terminal:

```bash
alias get_idf='source ~/ardupilot_esp32_build/ardupilot/modules/esp_idf/export.sh'
```

Then, once:

```bash
cd ~/ardupilot_esp32_build/ardupilot

get_idf                                              # load the tools

python3 -m pip install empy==3.3.4 pexpect           # once per environment
```

**Check:** `get_idf` ends with `Done!`, and `which python3` now points into `~/.espressif/python_env/`. Run `get_idf` in every new terminal before building.

## Part 4: Board files and the ESP32 fixes

ArduPilot builds a board from two files in its own folder: `hwdef.dat` (pins and sensors) and `defaults.parm` (parameters baked into the firmware, applied to every parameter you haven't saved yet). This Part creates both for `esp32lukas6050`, then applies the source fixes. Every step is safe to run again.

### 4.1 Create the board folder and `hwdef.dat`

Change `WIFI_SSID` and `WIFI_PWD` (8+ characters) before you run it.

```bash
cd ~/ardupilot_esp32_build/ardupilot

mkdir -p libraries/AP_HAL_ESP32/hwdef/esp32lukas6050

cat > libraries/AP_HAL_ESP32/hwdef/esp32lukas6050/hwdef.dat <<'EOF'
# esp32lukas6050: WEMOS D1 Mini ESP32 (or DevKit) + MPU6050 (GY-521) or GY-91 on I2C, indoor MTF-01 build
define HAL_ESP32_BOARD_NAME "esp32-lukas-6050"

# --- I2C bus 0 on the standard ESP32 I2C pins (NOT IO12: it is a boot strapping pin)
#             Port       SDA         SCL          SPEED    INTERNAL SOFT
ESP32_I2CBUS  I2C_NUM_0  GPIO_NUM_21 GPIO_NUM_22  400*KHZ  false    false

# --- IMU: MPU6050, or the GY-91's MPU9250, at I2C address 0x68 (AD0/SAO low)
define HAL_INS_DEFAULT HAL_INS_MPU60XX_I2C
IMU Invensense I2C:0:0x68 ROTATION_NONE
define INS_MAX_INSTANCES 3

# --- Barometer: the GY-91's BMP280 (or a BMP280 module) at 0x76; boots fine without one
BARO BMP280 I2C:0:0x76
define HAL_BARO_ALLOW_INIT_NO_BARO 1

# --- Compass: none used (the GY-91's AK8963 stays off); an external I2C compass on the same bus is found by probing
define AP_COMPASS_PROBING_ENABLED 1

# --- no airspeed sensor
define AP_AIRSPEED_ENABLED 0

# --- ADC pins (battery monitor, optional)
ESP32_ADC_PIN ADC1_GPIO35_CHANNEL 11   35
ESP32_ADC_PIN ADC1_GPIO34_CHANNEL 11   34
ESP32_ADC_PIN ADC1_GPIO39_CHANNEL 11   39
ESP32_ADC_PIN ADC1_GPIO36_CHANNEL 11   36

# --- WiFi access point (change SSID and password)
define HAL_ESP32_WIFI 1
define WIFI_SSID "lukas-drone"
define WIFI_PWD "changeme123"

# --- motor outputs 1-4
ESP32_RCOUT GPIO_NUM_25
ESP32_RCOUT GPIO_NUM_27
ESP32_RCOUT GPIO_NUM_33
ESP32_RCOUT GPIO_NUM_32

# --- RC input (PPM) on IO4
define HAL_ESP32_RCIN GPIO_NUM_4
define HAL_ESP32_RMT_RX_PIN_NUMBER 4

# --- UARTs: SERIAL0 = USB, UART1 on IO16 (RX) / IO17 (TX) = MTF-01 (SERIAL3)
ESP32_SERIAL  UART_NUM_0  GPIO_NUM_3    GPIO_NUM_1
ESP32_SERIAL  UART_NUM_1  GPIO_NUM_16   GPIO_NUM_17

# --- SD card / logging: same as the tested esp32lukas board (the MINI32 has no card; boot errors are harmless)
define AP_FILESYSTEM_ESP32_ENABLED 1
define HAL_ESP32_SDMMC 1
define HAL_ESP32_SDCARD 1
define LOGGER_MAVLINK_SUPPORT 1
define HAL_LOGGING_BACKENDS_DEFAULT 1

# --- optical flow (MTF-01): needs the esp32.h fix (4.3)
define AP_OPTICALFLOW_ENABLED 1
EOF
```

What the IMU lines mean: `IMU Invensense I2C:0:0x68` tells ArduPilot to look for an Invensense chip (MPU6000/6050 family) on I2C bus 0 at address 0x68. ArduPilot's own `esp32imu_module_v11` board uses exactly this line. `BARO BMP280 I2C:0:0x76` does the same for the barometer; change it to `0x77` if your BMP280's SDO pin is tied high. One firmware covers both options: the IMU line also finds the GY-91's MPU9250 (WHO\_AM\_I 0x71) or MPU6500 (0x70) through the same driver, and the BARO line finds the GY-91's BMP280 (ArduPilot's BMP280 driver accepts chip ID 0x58, or 0x60 for a BME280). With no barometer fitted, HAL\_BARO\_ALLOW\_INIT\_NO\_BARO lets the board boot anyway, so option A needs no change either.

### 4.2 Create `defaults.parm`

```bash
cat > ~/ardupilot_esp32_build/ardupilot/libraries/AP_HAL_ESP32/hwdef/esp32lukas6050/defaults.parm <<'EOF'
# esp32lukas6050 defaults (MPU6050 or GY-91 + MTF-01, indoor). Applied to every never-saved parameter.

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

# --- MTF-01 on SERIAL3 (IO16 RX / IO17 TX)
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
EK3_SRC1_YAW 0
EOF
```

Why no yaw source: the MPU6050 has no compass. ArduPilot's EKF can use optical flow without a heading (`AP_NavEKF3_Control.cpp`: "do not need the yaw angle aligned to use optical flow"), and the heading then comes from the gyro alone and drifts slowly. Stabilize and AltHold don't care. Loiter without a compass is less proven, so Part 8 tests it last and carefully. With an external compass, set `COMPASS_ENABLE 1` and `EK3_SRC1_YAW 1` instead.

### 4.3 Fix 1: turn optical flow back on

ArduPilot's `libraries/AP_HAL/board/esp32.h` forces `AP_OPTICALFLOW_ENABLED 0` for every ESP32 board, after your board file is read. This wraps it so your board's `define AP_OPTICALFLOW_ENABLED 1` wins. It does nothing if it's already done.

```bash
cd ~/ardupilot_esp32_build/ardupilot

F=libraries/AP_HAL/board/esp32.h

grep -q "#ifndef AP_OPTICALFLOW_ENABLED" $F || \
  sed -i 's/^#define AP_OPTICALFLOW_ENABLED 0$/#ifndef AP_OPTICALFLOW_ENABLED\n#define AP_OPTICALFLOW_ENABLED 0\n#endif/' $F

grep -n -B1 -A1 "AP_OPTICALFLOW_ENABLED" $F
```

**Check:** three lines in a row: `#ifndef AP_OPTICALFLOW_ENABLED`, `#define AP_OPTICALFLOW_ENABLED 0`, `#endif`.

### 4.4 Fix 2: motors stuck at idle

The pre-takeoff check that releases the motors after arming (`ArduCopter/takeoff_check.cpp`) is wrapped in `#if HAL_WITH_ESC_TELEM`, which is 0 on every ESP32 board, so it compiles to an empty function and the motors stay at `MOT_SPIN_MIN`. This keeps only the ESC-telemetry parts conditional. It prints `fixed`, or `already fixed - nothing to do`.

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

grep -n "#if\|#else\|no ESC telemetry" ArduCopter/takeoff_check.cpp
```

**Check:** the grep shows `#if FRAME_CONFIG != HELI_FRAME`, two `#if HAL_WITH_ESC_TELEM`, one `#else` and `// no ESC telemetry: nothing to check`. If the script stops with `file differs from expected`, ArduPilot has probably merged its own fix: check the file no longer starts with `#if HAL_WITH_ESC_TELEM && FRAME_CONFIG != HELI_FRAME`, then skip this step. (The GY-91 guide's 3.2b also has this fix as a patch file.)

### 4.5 Fix 3: ESC calibration

Nothing to build. `defaults.parm` sets `ESC_CALIBRATION 9`, and Part 7 calibrates the ESCs with a sketch. **Never press "Calibrate ESCs" in Mission Planner** on this board: it makes the ESP32 re-enter calibration at every power-up and stop talking to Mission Planner (recovery is in Troubleshooting).

### 4.6 Check the parameter names

A misspelled name in `defaults.parm` is silently ignored, so check them against ArduCopter's parameter list:

```bash
cd ~/ardupilot_esp32_build/ardupilot/Tools/autotest/param_metadata

python3 param_parse.py --vehicle ArduCopter --format json > /dev/null   # creates apm.pdef.json

cd ~/ardupilot_esp32_build/ardupilot

python3 - <<'EOF'
import json
d = json.load(open('Tools/autotest/param_metadata/apm.pdef.json'))
names = {k for grp in d.values() for k in grp}
bad = [l.split()[0] for l in open('libraries/AP_HAL_ESP32/hwdef/esp32lukas6050/defaults.parm')
       if l.split('#')[0].strip() and l.split()[0] not in names]
print("Unknown parameters:", bad if bad else "none")
EOF
```

**Check:** it prints `Unknown parameters: none`.

## Part 5: Build, check and flash

### 5.1 Build

```bash
cd ~/ardupilot_esp32_build/ardupilot

get_idf                                       # load the ESP32 tools

./waf configure --board=esp32lukas6050        # choose this board

./waf copter                                  # build ArduCopter (about 10 minutes the first time)
```

**Check:** it ends with `'copter' finished successfully` and an `ardupilot.bin binary size` line showing about 33% free. If `waf` stops at `esp-idf_showinc` with `constraints ... doesn't exist`, run `export IDF_PYTHON_CHECK_CONSTRAINTS=no`, delete `build/esp32lukas6050/esp-idf_build`, and build again.

### 5.2 Confirm the IMU, flow, throttle fix and defaults are inside

Do this before every flash that follows a board-file or source change.

```bash
cd ~/ardupilot_esp32_build/ardupilot

grep "HAL_INS_PROBE1" build/esp32lukas6050/hwdef.h                                    # IMU on I2C bus 0, 0x68

nm -C build/esp32lukas6050/esp-idf_build/ardupilot.elf | grep -c AP_OpticalFlow_MAV   # above 0: flow driver in

nm -C -S build/esp32lukas6050/esp-idf_build/ardupilot.elf | grep " Copter::takeoff_check()"   # size NOT 00000005

grep -c "defaults.parm" build/esp32lukas6050/ap_romfs_embedded.h                      # 1: defaults embedded
```

**Check:**

| Line | Must show |
| --- | --- |
| `HAL_INS_PROBE1` | `AP_InertialSensor_Invensense::probe(*this,GET_I2C_DEVICE(0,0x68),ROTATION_NONE)` |
| `AP_OpticalFlow_MAV` | A number above 0. A 0 means 4.3 didn't run: do it, configure and build again |
| `Copter::takeoff_check()` | A size other than `00000005` (the test build showed `000000a4`). `00000005` is an empty function: do 4.4, then build again |
| `defaults.parm` | `1` |

### 5.3 Flash

Flash the **bare board** (nothing but USB connected). The first flash of this board must erase the flash, so `defaults.parm` applies to every parameter.

**On Windows with WSL2**, first pass the USB port to Ubuntu. In an **Admin PowerShell**:

```powershell
winget install --interactive --exact dorssel.usbipd-win   # once; then reopen PowerShell

usbipd list                                               # find the board (CP2104 = 10c4:ea60); note its BUSID, e.g. 1-1

usbipd bind --busid 1-1                                   # once per board

usbipd attach --wsl --busid 1-1                           # every time you plug it in; keep Ubuntu open
```

Then in **Ubuntu**:

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null                  # find the port, e.g. /dev/ttyUSB0

sudo usermod -aG dialout $USER                            # once; then log out and back in

cd ~/ardupilot_esp32_build/ardupilot

get_idf

esptool.py --chip esp32 -p /dev/ttyUSB0 erase_flash       # first flash of this board only

ESPPORT=/dev/ttyUSB0 ESPBAUD=921600 ./waf copter --upload
```

**Check:** it ends with `Hash of data verified` and `Hard resetting`. Close Mission Planner and any serial monitor first: only one program can hold the port.

**If flashing won't start** (`Failed to connect`), put the board in download mode by hand: jumper **IO0 to GND**, press and release **RST**, then:

```bash
cd ~/ardupilot_esp32_build/ardupilot/build/esp32lukas6050/esp-idf_build

esptool.py --chip esp32 -p /dev/ttyUSB0 --before no_reset erase_flash

# press and release RST again (jumper still in), then:
esptool.py --chip esp32 -p /dev/ttyUSB0 --before no_reset -b 460800 write_flash @flash_args
```

Remove the jumper and press RST to start the firmware.

### 5.4 Check that it boots

```bash
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200      # quit with Ctrl+]
```

Press **RST** and watch the log. The first line `rst:0x1 (POWERON_RESET)` is normal. A reset that repeats every few seconds (`rst:0xc`, `0x7`, `0x8` or `0xf`) means a crash, watchdog or brownout: read Troubleshooting before going further. `sdmmc_card_init failed` lines are harmless (no SD card).

**Check:** the log stops resetting, and after you connect Mission Planner (Part 6) the Messages tab shows no `INS: unable to initialise driver`. If you see that message, the IMU wasn't found at 0x68: rerun the Part 2 sketch.

## Part 6: Mission Planner setup and calibration

Props **off** for this whole Part. Reboot the board (press RST) after any group of parameter changes.

### 6.1 Connect

- **USB (best for setup):** pick the board's COM port and **115200** (not AUTO). In **Config → Planner**, tick only **Reset on USB connect (DTR)**; untick it too if connecting is slow.
- **WiFi (good for watching values during tests):** join your `WIFI_SSID` network, then connect with **TCP**, host **192.168.4.1**, port **5760**.

**Check:** the HUD shows **DISARMED** and the Messages tab shows the IMU starting, with no `INS: unable to initialise driver`.

### 6.2 Set the parameters that need a reboot first

In **Config → Full Parameter List**, set and **Write Params**, then reboot:

| Parameter | Value | Why |
| --- | --- | --- |
| `MAV3_OPTIONS` | 2 | Don't forward MAVLink traffic to the MTF-01. MAV numbers count MAVLink ports in order: USB = MAV1, WiFi = MAV2, MTF-01 = MAV3. Appears after the first reboot |
| `SERIAL3_OPTIONS` | 0 | Leave at 0: the old bit 10 (1024) blocks arming on 4.7+ firmware |
| `FLOW_ORIENT_YAW` | 0 | Starting value; 6.5 tells you whether to change it |

The rest (loop rate, frame, flow, rangefinder, EKF sources, arm switch) is already set by `defaults.parm`. Confirm `SCHED_LOOP_RATE` = 150 and `FLOW_TYPE` = 5 to be sure the defaults applied; if they didn't, the flash wasn't erased (5.3).

### 6.3 Calibrate

In **Setup → Mandatory Hardware**, over USB:

| Step | How | Check |
| --- | --- | --- |
| Accelerometer | **Calibrate Accel** (six positions), then **Calibrate Level** with the frame on a level surface | HUD level when the frame is level; no `DCM Roll/Pitch inconsistent` message |
| Radio | **Calibrate Radio**: move every stick to its corners and every switch through every position, then **Click when Done** | Throttle stick moves bar 3 (about 1000 to 2000); roll, pitch, yaw on bars 1, 2, 4 |
| Flight modes | **Setup → Flight Modes**: flip the CH5 switch | The highlighted mode changes (Stabilize / AltHold) |
| Arm switch | **Data → Status → `ch6in`** while you flip CH6 | Goes **below 1200** and **above 1800**; if not, set the radio's CH6 end points to 100% |

Skip **Compass**: there is none on this build (6.6). Skip **ESC Calibration**: Part 7.

### 6.4 Check the IMU inside ArduPilot

Always **power up with the drone sitting still**: ArduPilot measures the gyro's zero point in the first seconds.

Put the frame on a level table, USB only, and watch for 5 minutes:

| Value (Data → Status) | Good |
| --- | --- |
| HUD `roll` / `pitch` | Steady within about ±1° |
| `ax`, `ay` | Near 0 and within about ±10 milli-g (the GY-91 showed about ±20) |
| `az` | Near -1000 (1 g) |
| `gx`, `gy`, `gz` | Near 0, small noise, no steady swing |

If it fails here but passed the Part 2 sketch, the problem is the mount or the wiring on the frame, not the chip.

**Orientation check (both options).** With the frame level and the HUD level, tilt the nose down: the HUD should pitch nose down. Lift the left side: the HUD should roll right. If either is wrong or swapped, the IMU is turned on the frame. Set `AHRS_ORIENTATION` by where the board's forward (Y) arrow points: right = 2, backward = 4, left = 6. Reboot, check again, then redo Calibrate Accel and Calibrate Level (6.3).

### 6.4b Option B: check the barometer

Only for the GY-91 (or an add-on BMP280).

1. Messages tab after boot: `Calibrating barometer`, then `Barometer 1 calibration complete`. No such line means the BMP280 wasn't found at 0x76: run the Part 2 scan.
2. Data → Status: `press_abs` about 1000–1015 hPa near sea level (lower if you live higher up), and `press_temp` near room temperature (a few degrees warmer is normal).
3. Lift the drone from the floor to above your head (about 1.5 m) and back: `press_abs` drops by about 0.2 hPa and comes back (near sea level, 1 m is about 0.12 hPa).

Check: all three pass. This board won't warn you about a missing or broken barometer at arming: `HAL_BARO_ALLOW_INIT_NO_BARO` skips ArduPilot's barometer arming check (`AP_Arming.cpp`), so this check is the only one you get.

### 6.5 MTF-01 checks

Configure the MTF-01 in **MicoAssistant** before connecting it: output protocol `mav_apm`, `mav_id` 200 (anything except 1), baud 115200.

**Data check** (hold the drone 30–50 cm over a textured floor, in normal room light):

| Value | Good |
| --- | --- |
| `rangefinder1` | Follows the height (about 30 at 30 cm, 50 at 50 cm) |
| `opt_qua` | Well above 0 (0 = can't track: plain floor, dim light, or the lens covered) |
| Messages | No `PreArm: Rangefinder 1: No Data` |

**Orientation (tilt) test.** `opt_m_x` and `opt_m_y` are the flow rate minus the drone's own rotation (`GCS_Common.cpp`, `send_opticalflow`). Tilting the drone over a still floor should cancel out:

1. Hold the drone level about 40 cm over the textured floor.
2. Without moving it sideways, rock it **left and right** about ±20° once a second: watch `opt_m_x`.
3. Rock it **nose down and nose up**: watch `opt_m_y`.

| While rocking | Meaning | Do |
| --- | --- | --- |
| Both stay small (under about 0.3) | Correct | Keep `FLOW_ORIENT_YAW 0` |
| Both swing large (about 1–3) | Reversed 180° | `FLOW_ORIENT_YAW = 18000`, reboot, repeat |
| Rolling moves `opt_m_y` instead | Rotated 90° | Try `9000`, then `-9000`; keep the one that passes |
| Only one axis swings large | Mirrored | Check the mounting and the MicoAssistant protocol; a rotation can't fix it |

An inverted flow orientation makes ArduPilot push the drone harder in the direction it drifts, which flips it in Loiter. Don't fly Loiter until this passes.

**Move test:** right-click the map → **Set EKF Origin Here**. Carry the drone level at about 50 cm, 1 m forward, then 1 m right. The map icon should follow, and stay put when you stop.

### 6.6 Heading without a compass

With `EK3_SRC1_YAW 0` the heading comes from the gyro only, so the HUD heading **drifts slowly** and won't match a phone compass. That is expected. A constant or slowly drifting heading doesn't upset flow-based flying, because flow readings and motor commands are turned by the same amount.

**Adding an external compass later** (QMC5883L or HMC5883L on IO21/IO22, mounted away from power wires): set `COMPASS_ENABLE 1` and `EK3_SRC1_YAW 1`, reboot, check it's detected, calibrate it in **Setup → Compass** away from steel, then check the HUD heading against a phone at N, E, S, W (within about 15°).

## Part 7: ESC calibration

The ESCs must learn that 1000 µs means stop and 2000 µs means full, the same range `MOT_PWM_MIN` / `MOT_PWM_MAX` send. ArduPilot's own calibration hangs the ESP32 (4.5), so this sketch does it instead. It drives all four motor pins at once and tells you when to plug in the battery. Afterwards, flash ArduPilot back (5.3, without `erase_flash`).

Arduino IDE, **ESP32 core 3.x**, board **ESP32 Dev Module**, Serial Monitor 115200 with line ending **Newline**. Press Enter in the input box to answer.

```cpp
// GUIDED ESC CALIBRATION for 4 ESCs - no ArduPilot. PROPS OFF.
// To start over: unplug the battery, press the ESP32's EN/RST button.

const int ESC_PINS[4] = {25, 27, 33, 32};   // motor 1-4 pins from the board file

void setPulse(int us) {
  for (int i = 0; i < 4; i++) {
    ledcWrite(ESC_PINS[i], (uint32_t)us * 65535 / 20000);   // 50 Hz, pulse width in us
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
  delay(1000);
  for (int i = 0; i < 4; i++) ledcAttach(ESC_PINS[i], 50, 16);
  setPulse(1000);                                         // safe start

  Serial.println("\n=========== ESC CALIBRATION ===========");
  Serial.println("STEP 1: Check ALL of these:");
  Serial.println("  - PROPS OFF");
  Serial.println("  - Battery UNPLUGGED");
  Serial.println("  - ESC signal wires on IO25, IO27, IO33, IO32");
  Serial.println("  - ESC black/brown signal ground wires on ESP32 GND");
  Serial.println("  - ESC red wires NOT connected (ESP32 runs from USB)");
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
  Serial.println("   unplug the battery, press EN/RST, and start again.");
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

| Serial Monitor says | You do | You hear |
| --- | --- | --- |
| STEP 1 | Props off, battery unplugged, wires as listed; press Enter | — |
| STEP 2 | Plug in the battery; press Enter as soon as you hear "beep-beep" | Startup tune, then beep-beep |
| STEP 3 | Press Enter after the long beep | 3 beeps (3S), then 1 long beep |
| STEP 4 | Press Enter; the motors ramp 1100 → 1500 µs over 10 s | All 4 motors speed up together |
| STEP 5 | Unplug the battery | — |

**ESC beep codes** (generic plane-style 40A ESCs):

| You hear | Meaning | Fix |
| --- | --- | --- |
| One beep about every 2 s | No signal | Signal wire on the right pin; ESC signal ground to ESP32 GND |
| Fast continuous beeping | Throttle not at minimum | Calibrate (above) |
| Double beeps repeating | Battery voltage out of range | Fully charged pack |

**`RC_SPEED`** (default 490, capped to 400 Hz by the ESP32 output driver): plane-style ESCs are built for a 50 Hz receiver signal. Keep the default if Motor Test follows smoothly. If a motor stutters or ignores higher %, set `RC_SPEED = 50` and reboot, then step up 100 → 200 → 300 and keep the highest value that still responds.

**Check:** after flashing ArduPilot back, **Motor Test** at 10% spins each motor smoothly, and at 50% clearly faster.

## Part 8: Bench test, then indoor flight in stages

### 8.1 Bench test (props OFF)

Work through every item; fix any failure before moving on.

- [ ] Boot log stops resetting; Messages show no `INS: unable to initialise driver` (option B: Barometer 1 calibration complete, 6.4b)
- [ ] IMU still check passes (6.4): HUD steady within about ±1°
- [ ] Radio bars correct (6.3); CH5 switches Stabilize / AltHold
- [ ] Transmitter off → Mission Planner reports a radio failsafe
- [ ] MTF-01 data, tilt test and move test pass (6.5)
- [ ] **Motor Test** (Setup → Optional Hardware) at 10%: A = front-right, B = rear-right, C = rear-left, D = front-left, each spinning the way the Part 1 motor table says (swap any two of a motor's three wires to reverse it)
- [ ] **Arm:** after boot, flip CH6 **down, then up**. HUD shows **ARMED**, and **Data → Status** shows `ch1out` to `ch4out` at about **1100** with the stick down
- [ ] **Throttle:** push the stick up slowly. `ch1out` to `ch4out` rise **together** toward about 1900 and the motors speed up
- [ ] **Stabilisation:** at about 30% throttle, tilt the frame by hand; the motors on the low side speed up
- [ ] **Disarm:** CH6 down stops the motors at once

What the output numbers mean when you think it's armed:

| `ch1out`–`ch4out` with stick up | Meaning |
| --- | --- |
| Rise toward 1900 | Working |
| Stay at **1000** | **Not armed** (1000 = disarmed). Motor Test still works because it arms the motors itself. Flip CH6 down then up, and read the Arm:/PreArm: message |
| Stay at **1150** | The motors-stuck-at-idle fix (4.4) is missing from this firmware |
| Split (two high, two low) | The attitude estimate thinks the drone is tilted: redo Calibrate Level, check the IMU mount |

### 8.2 Props on

- Each prop matches its motor's spin direction, printed side up, with the thicker leading edge going first.
- **Tether** the drone (or hold it down) for the first throttle-ups, with your finger on the CH6 disarm switch.
- Fly over a **textured floor** in good light, between about 0.3 and 2 m.

### 8.3 Fly in stages

| Stage | Mode | Pass when | Then |
| --- | --- | --- | --- |
| 1 | **Stabilize**, tethered, 0.5–1 m | Hovers without fighting you; in Status (over WiFi), `vibex/y/z` below about 15–20 and `clip0` not rising | Hover 1–2 minutes so `MOT_THST_HOVER` learns the hover throttle (it moves away from 0.35) |
| 2 | **AltHold**, throttle stick at **mid** | Holds height with the stick centred; climbs above mid, descends below | If it pumps up and down, check vibration (`vibez`) and `rangefinder1` |
| 3 | **Loiter** (experimental without a compass) | Holds position hands-off | Only after 6.5 passed; see below |

**Stage 3 procedure:** set `FLTMODE6 5` (Loiter), reboot, take off in **AltHold** to about 1 m, then switch to Loiter with your finger on the mode switch. If it leans or accelerates at all, switch straight back to AltHold and land. ArduPilot's optical-flow guide asks for about 15 m of clear space for the first position-hold test; use the largest space you have.

**Logs:** `LOG_BACKEND_TYPE 0` means nothing is recorded. Before tuning or chasing a misbehaving Loiter, fit an SD card (and set `LOG_BACKEND_TYPE 1`) or log over MAVLink (`LOG_BACKEND_TYPE 2`, needs a log receiver connected): one log shows what the flow sensor, EKF and motors did.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Board won't boot, or boot-loops right after wiring the IMU | Something with a pull-up on IO12 (boot pin) | Move it to IO21/IO22 as in Part 1; keep IO12 free |
| `INS: unable to initialise driver` | IMU not found at 0x68 | Run the Part 2 sketch: check the I2C scan shows 0x68 and `WHO_AM_I` is 0x68 (MPU6050) or 0x70 / 0x71 / 0x73 (GY-91); check SDA/SCL aren't swapped and AD0 is low |
| `WHO_AM_I` is not 0x68 / 0x70 / 0x71 / 0x73 | A clone ArduPilot's Invensense driver doesn't recognise | Use a different module |
| GY-91: the scan shows 0x69 or 0x77 | SDO/SAO is floating high | Tie SDO/SAO to GND |
| GY-91: the scan misses 0x68 or 0x76 | NCS or CSB isn't high, so that chip isn't in I2C mode | Tie NCS and CSB to the GY-91's 3V3 pin |
| No Barometer 1 calibration complete message; press\_abs reads 0 | BMP280 not found at 0x76 | Run the Part 2 scan for 0x76; check the two GY-91 rows above |
| HUD rocks or drifts while sitting still | Noisy chip, soft or loose mount, long wires, or moved during power-up | Part 2 sketch on a towel first; then firm mount, short wires, power up still; redo Calibrate Level |
| `PreArm: Rangefinder 1: No Data`; ground speed and height wander while still | The EKF has lost the MTF-01 (it is its height and velocity source) | Check MTF-01 wiring (TX → IO16, RX → IO17), MicoAssistant settings, `SERIAL3_PROTOCOL 1`, `SERIAL3_BAUD 115`, `MAV3_OPTIONS 2`. For bench tests without it: `EK3_SRC1_POSZ 1`, `EK3_SRC1_VELXY 0` |
| `FLOW_TYPE` missing from the parameter list | Flow compiled out | Do 4.3, configure and build again, check 5.2 |
| Armed, motors idle, throttle does nothing; `ch1out`–`ch4out` stay at 1150 | Motors-stuck-at-idle bug | Do 4.4, rebuild, check the `takeoff_check` size (5.2), flash |
| `ch1out`–`ch4out` stay at 1000 while you think it's armed | Not armed | HUD must say ARMED; flip CH6 down then up; `ch6in` must pass 1200 and 1800 |
| Board stopped answering Mission Planner after "Calibrate ESCs" | ESC calibration re-runs and hangs at every power-up | Wipe only the parameter storage: `esptool.py --chip esp32 -p /dev/ttyUSB0 erase_region 0x310000 0x40000` (IO0 to GND if it won't connect), then redo Part 6 and use Part 7 |
| ESC beeps once every 2 s | No PWM signal | Signal wire on the right pin; ESC signal ground to ESP32 GND |
| Motor Test works but motors stutter or ignore higher % | ESC can't follow 400 Hz | `RC_SPEED 50`, then step up (Part 7) |
| AltHold pumps up and down | Vibration, wrong hover throttle, or noisy rangefinder | `vibez` below about 15–20; hover longer so `MOT_THST_HOVER` learns; check `rangefinder1` |
| Loiter leans hard or flips | Flow orientation inverted, or a bad attitude or heading estimate | Back to AltHold; redo the tilt test (6.5); fix `FLOW_ORIENT_YAW`; get a log before trying again |
| `No MAVLink Heartbeat Packets` | Port held by another program, AUTO baud, or reset on connect | Close other serial programs; 115200 not AUTO; Config → Planner reset options (6.1) |
| Board resets when throttle goes up (`rst:0xf`) | 5V brownout | Stronger 5V supply, capacitor across 5V and GND |
| `waf` fails with `constraints ... doesn't exist` | ESP-IDF Python check | `export IDF_PYTHON_CHECK_CONSTRAINTS=no`, delete `build/esp32lukas6050/esp-idf_build`, build again |

## Sources

**ArduPilot pull requests (the motor-idle bug)**

- [ArduPilot PR #34070](https://github.com/ArduPilot/ardupilot/pull/34070): takeoff check blocks spool-up on boards without ESC telemetry
- [ArduPilot PR #34132](https://github.com/ArduPilot/ardupilot/pull/34132): related fix for the same spool-up block

**ArduPilot documentation**

- [Optical flow sensor setup](https://ardupilot.org/copter/docs/common-optical-flow-sensor-setup.html)
- [MicoAir MTF-01](https://ardupilot.org/copter/docs/common-mtf-01.html)
- [Brushless ESCs (PWM, RC\_SPEED)](https://ardupilot.org/copter/docs/common-brushless-escs.html)
- [FlowHold mode](https://ardupilot.org/copter/docs/flowhold-mode.html)
- [AP\_HAL\_ESP32 README](https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_HAL_ESP32/README.md)

**Forum threads**

- [Copter without GPS AND Compass](https://discuss.ardupilot.org/t/copter-without-gps-and-compass-re-ask/139756)
- [Can't throttle up beyond arming](https://discuss.ardupilot.org/t/cant-throttle-up-beyond-arming/43941)
- [450mm ESP32 Quadcopter Success](https://discuss.ardupilot.org/t/450mm-esp32-quadcopter-success/142049): a GY-91 on I2C flying Stabilize and AltHold on an ESP32
- [Jehanzeb1973/ESP32-ArduPilot](https://github.com/Jehanzeb1973/ESP32-ArduPilot): the precompiled firmware used there (its I2C\_MPU9250 build is ArduCopter V4.5.0-dev, with the same 0x68 IMU and 0x76 BMP280 lines, and no optical-flow driver)

**ArduPilot source files checked (master, October 2026)**

- `libraries/AP_InertialSensor/AP_InertialSensor_Invensense.cpp`: WHO\_AM\_I list (0x68 = MPU6000/6050) and axis mapping (body X = chip Y, body Y = chip X, body Z = −chip Z)
- `ArduCopter/takeoff_check.cpp` and `libraries/AP_Motors/AP_MotorsMulticopter.cpp`: the spool-up block and the `HAL_WITH_ESC_TELEM` guard
- `libraries/AP_HAL_ESP32/Scheduler.cpp`: why the ESC-calibration flag never gets saved
- `libraries/AP_HAL/board/esp32.h`: optical-flow default
- `libraries/AP_HAL_ESP32/hwdef/esp32imu_module_v11/hwdef.dat`: upstream MPU60xx-over-I2C example
- `libraries/AP_HAL_ESP32/hwdef/esp32s3m5stampfly/`: the only upstream flying ESP32 copter (compass off by default)
- `libraries/GCS_MAVLink/GCS_Common.cpp` (`send_opticalflow`): what `opt_m_x/y` mean
- `libraries/AP_NavEKF3/AP_NavEKF3_Control.cpp`: flow aiding works without yaw alignment
- `libraries/AP_NavEKF3/AP_NavEKF3_PosVelFusion.cpp` (`selectHeightForFusion`): switches height to the barometer when the rangefinder has sent nothing for 500 ms
- `libraries/AP_Baro/AP_Baro_BMP280.cpp`: accepts chip ID 0x58 (BMP280) and 0x60 (BME280)
- `libraries/AP_Arming/AP_Arming.cpp` (`barometer_checks`): skipped on boards with `HAL_BARO_ALLOW_INIT_NO_BARO`

**Datasheets**

- InvenSense MPU-6000/MPU-6050 Product Specification: accel noise 400 µg/√Hz, gyro noise 0.005 °/s/√Hz
- InvenSense MPU-9250 Product Specification: accel noise 300 µg/√Hz, gyro noise 0.01 °/s/√Hz
