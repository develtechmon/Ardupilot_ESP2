# ArduPilot on ESP32 + GY-91 (with compass) — Indoor MTF-01 Build Guide

Oct 14, 2026 · @lukas

## At a glance

This guide builds an indoor ArduCopter flight controller from a WEMOS D1 Mini ESP32 (MINI32), a **GY-91** (MPU9250 IMU, BMP280 barometer and AK8963 compass, all on I2C) and a MicoAir MTF-01 optical-flow and range sensor. The compass is **on from the first flash**, and every ESP32 fix found so far is built in, including ESC calibration from Mission Planner.

> **Status and safety.** ArduPilot on ESP32 is experimental. Keep props **off** until Part 8, fly tethered first, and never near people. On the bench this build connects to Mission Planner, loads parameters quickly, the HUD follows the IMU, the BMP280 is found, and Mission Planner's ESC calibration completes. Indoors, with the compass enabled exactly as this guide sets it up (`COMPASS_ENABLE 1`, `EK3_SRC1_YAW 1`), **AltHold and PosHold have flown and held**. On your own frame, still run the heading check (6.5) and compassmot (8.2) before the first PosHold flight, because motor wiring changes compass interference from frame to frame.

**Tested version.** ArduPilot master at commit `9dd2e0f3`, built with ESP-IDF 5.3. `git describe` prints `ArduPilot-4.6.0-beta1-9022-g9dd2e0f350` (9022 commits after the last tag it can see), while the firmware calls itself `ArduCopter V4.8.0-dev` in the boot log: both name the same code. The board files were also test-built on master commit `81ff4d6a` (6 October 2026).

**How to use this guide:** work through the Parts in order. Each step ends with a **Check**; don't move on until it passes. In command blocks, a blank line separates commands, a line ending in `\` continues on the next line, and text after `#` is a comment.

**What this build is:**

|  | This build (`esp32lukasgy91`) |
| --- | --- |
| IMU | MPU9250 inside the GY-91, I2C address 0x68 |
| Barometer | BMP280 inside the GY-91, I2C 0x76 (backup height source) |
| Compass | AK8963 inside the MPU9250, I2C 0x0C, on by default (`COMPASS_ENABLE 1`, `EK3_SRC1_YAW 1`) |
| I2C pins | SDA **IO23**, SCL **IO18** (the tested wiring) |
| Flow and height | MTF-01 on SERIAL3 (IO16/IO17): flow velocity + rangefinder height |
| RC input | FS-A8S PPM on IO4 |
| Motors | IO25, IO27, IO33, IO32; plain PWM at 50 Hz (`RC_SPEED 50`) |
| Loop rate | `SCHED_LOOP_RATE 150` |

**The five ESP32 fixes** (all needed, all in Part 4):

1. **Optical flow is switched off for every ESP32 board** in ArduPilot's `esp32.h`. Without the fix, `FLOW_TYPE` doesn't exist.
2. **Motors stuck at idle.** From ArduPilot 4.7 the motor library locks the motors at ground idle after arming, and on ESP32 nothing releases the lock: the outputs sit at 1150 and the throttle does nothing (upstream PR #34070 / #34132).
3. **The I2C IMU reader runs at the lowest priority.** ArduPilot's ESP32 code gives the I2C bus task priority 5 (SPI gets 24), so parameters load slowly and the HUD lags or freezes. The fix raises it to 24.
4. **ESC calibration from Mission Planner hangs the ESP32.** On ESP32, parameters are only saved after startup finishes, and calibration never finishes startup, so the board re-enters calibration at every power-up. The fix saves the cleared flag straight to flash.
5. **The compass must be searched for after the IMU starts.** The AK8963 sits behind the MPU9250, which only opens the way to it ("I2C bypass") once the IMU driver starts, so the board file uses the `probe_mpu9250` compass line.

## Part 1: Parts and wiring

The GY-91 needs only four wires (VIN, GND, SDA, SCL): its board pulls the address and mode pins to the right levels on its own.

### 1.1 Parts

| Part | Notes |
| --- | --- |
| WEMOS D1 Mini ESP32 (MINI32) | Classic ESP32 (ESP32-WROOM-32, 4 MB flash). A classic ESP32 DevKit works with the same pins |
| GY-91 (MPU9250 + BMP280) | I2C. Needs a genuine MPU9250 for the compass: Part 2 checks it |
| MicoAir MTF-01 | Optical flow + rangefinder, MAVLink on a UART |
| FlySky FS-A8S receiver + transmitter | PPM output |
| 4 × brushless ESC (40A, 5V BEC) + A2212 1400KV + 8045 props | Plain PWM ESCs, calibrated from Mission Planner (Part 7) |
| 3S LiPo | 11.1V nominal |
| Short jumper wires, open-cell foam, soft foam tape | Keep the GY-91 wires under about 10 cm |

### 1.2 Pin table

| Device pin | MINI32 pin | Notes |
| --- | --- | --- |
| GY-91 VIN / GND | 5V / GND | VIN feeds the GY-91's own 3.3V regulator |
| GY-91 3V3 | not connected | The regulator's output: don't feed power into it |
| GY-91 SDA | **IO23** | I2C data (IMU, barometer and compass share it) |
| GY-91 SCL | **IO18** | I2C clock |
| GY-91 SDO/SAO | not connected | Pulled low on the tested board: IMU at 0x68, barometer at 0x76. If the Part 2 scan shows 0x69 or 0x77, tie it to GND |
| GY-91 NCS, CSB | not connected | Pulled high on the board = I2C mode. If the scan misses 0x68 or 0x76, tie both to the GY-91's 3V3 pin |
| FS-A8S PPM | IO4 | Use the PPM output |
| FS-A8S + / − | 5V / GND |  |
| MTF-01 TX | IO16 | MINI32 RX, ArduPilot SERIAL3 |
| MTF-01 RX | IO17 | MINI32 TX |
| MTF-01 VCC / GND | 5V / GND |  |
| Motor 1–4 signal | IO25, IO27, IO33, IO32 | ESC signal wires |
| ESC signal grounds | GND | All four, for a clean PWM signal |
| 5V supply | 5V / GND | One ESC's BEC (red + black). Leave the other three ESCs' red wires unconnected |

**The I2C pins in your wiring, in `hwdef.dat` (4.1) and in the Part 2 sketch must be the same.** IO23/IO18 is the tested wiring; IO22 or IO21 work too if you change all three. If they differ, ArduPilot never finds the IMU, startup never finishes, and Mission Planner gets no heartbeat even though the WiFi network shows up.

**Wiring schematic**

&#91;embedded content: Wiring schematic · GY-91, MTF-01, receiver, 4 ESCs, power\]

Arrows show which way a signal travels; SDA and SCL carry data both ways. Every device's GND must join the same ground as the ESP32, or the signals have no reference.

### 1.3 Wiring rules

- **Never put I2C (or anything with a pull-up) on IO12.** IO12 is an ESP32 boot pin: pulled high at power-up, it sets the flash to 1.8V and the board won't boot. Also leave IO6–IO11 alone (internal flash).
- **Keep the GY-91 wires short** (under about 10 cm) and away from the ESC and battery wires. Twist SDA and SCL each with a GND wire if they must be longer.
- **Keep current-carrying wires away from the GY-91.** The compass is on that board: battery and ESC power leads close to it pull the heading as the motors draw current. Route them along the far side of the frame.
- **Cover the BMP280** (the small metal can with a pin-hole) with a piece of open-cell foam to keep prop wash off it. Don't seal it airtight.
- **USB and the BEC together:** the MINI32's 5V pin connects straight to USB 5V. During bench setup, unplug the BEC's red wire while USB is connected, or power the board from USB only.
- **Mount the GY-91 so ArduPilot reads it the right way.** ArduPilot's driver maps the chip's Y axis to the drone's forward direction (`AP_InertialSensor_Invensense.cpp`, lines 613–615). With `ROTATION_NONE`: **chip side up, Y axis forward, X axis right**. Mount it flat, near the centre of the frame, on firm foam. The orientation check in 6.4 confirms it, and `AHRS_ORIENTATION` fixes a board turned 90° or 180° (it turns the compass with it).
- **Mount the MTF-01** underneath, lens straight down, square to the frame, with a clear view of the floor (its camera sees a 42° cone). Part 6 checks its orientation.

**Motor layout** (quad X, viewed from above, front at the top). ArduPilot's numbering is not clockwise:

| Pin | Motor | Position | Spin | Motor Test button |
| --- | --- | --- | --- | --- |
| IO25 | 1 | front-right | CCW | A |
| IO27 | 2 | rear-left | CCW | C |
| IO33 | 3 | front-left | CW | D |
| IO32 | 4 | rear-right | CW | B |

**Check:** before powering up, confirm with a multimeter that 5V and GND aren't shorted, and that nothing is wired to IO12.

## Part 2: Test the GY-91 before you build

This sketch proves three things before ArduPilot is involved: the GY-91 answers on your pins, its chip is a genuine MPU9250 with a compass inside, and its readings are quiet while it sits still.

Arduino IDE, ESP32 board package, board **ESP32 Dev Module**, Serial Monitor at 115200. Put the board on a folded towel, away from the laptop, and don't touch it while it measures. The sketch must use the same SDA/SCL pins as the `ESP32_I2CBUS` line in 4.1.

```cpp
// GY-91 check for ArduPilot on ESP32: I2C scan, chip ID, compass, and a still-noise test.
// Wiring: SDA = IO23, SCL = IO18 (the same pins as ESP32_I2CBUS in hwdef.dat). Board on a towel, don't touch it.
#include <Wire.h>

const int SDA_PIN = 23, SCL_PIN = 18;
const uint8_t ADDR = 0x68;          // MPU9250, SDO/SAO low
const uint8_t MAG  = 0x0C;          // AK8963 inside the MPU9250
const int N = 500;                  // samples per test (about 2.5 s)
uint8_t who = 0;

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool readFrom(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;     // repeated start
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN, 400000);                    // 400 kHz like ArduPilot

  Serial.println("\n=== I2C scan ===");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("  device at 0x%02X\n", a);
  }

  readFrom(ADDR, 0x75, &who, 1);                           // WHO_AM_I
  Serial.printf("WHO_AM_I = 0x%02X : ", who);
  if (who == 0x71 || who == 0x73)  Serial.println("MPU9250/9255 - IMU and compass OK for this guide");
  else if (who == 0x70)            Serial.println("MPU6500 - IMU works, but there is NO compass inside");
  else                             Serial.println("NOT an ID ArduPilot's Invensense driver expects (clone?)");

  writeReg(0x6B, 0x80);                                    // PWR_MGMT_1: reset
  delay(100);
  writeReg(0x6B, 0x01);                                    // wake up, gyro clock
  writeReg(0x1A, 0x03);                                    // CONFIG: gyro low-pass about 44 Hz
  writeReg(0x1B, 0x00);                                    // GYRO_CONFIG: +-250 dps = 131 per dps
  writeReg(0x1C, 0x00);                                    // ACCEL_CONFIG: +-2 g = 16384 per g
  writeReg(0x1D, 0x03);                                    // ACCEL_CONFIG2: accel low-pass about 44 Hz

  writeReg(0x37, 0x02);                                    // INT_PIN_CFG: I2C bypass on, as ArduPilot does
  delay(10);
  uint8_t wia = 0;
  bool magOk = readFrom(MAG, 0x00, &wia, 1);               // AK8963 WIA register
  Serial.printf("Compass AK8963 at 0x0C: %s, WIA = 0x%02X (expect 0x48)\n",
                magOk ? "answers" : "NOT found", wia);
  delay(200);
}

void loop() {
  double s[6] = {0}, q[6] = {0};
  int good = 0;
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

**A good GY-91 prints:** `device at 0x68` and `device at 0x76` in the scan, `WHO_AM_I = 0x71` (0x73 for an MPU9255), `Compass AK8963 at 0x0C: answers, WIA = 0x48`, and `ACC noise GOOD   GYRO noise GOOD`. The compass doesn't show in the scan: it only appears after the sketch turns on the bypass, which is exactly what ArduPilot does.

**What the noise numbers should be.** With the 44 Hz low-pass the sketch sets, the MPU9250 datasheet's typical noise (300 µg/√Hz, 0.01 °/s/√Hz) works out to about **2.5 mg and 0.08 °/s**. The sketch calls GOOD up to about twice that. A healthy chip stays within about ±10 mg while still.

**Run it three times, then read the result:**

1. On the towel, USB only.
2. Fitted on the frame, USB only.
3. On the frame with the battery plugged in (ESCs powered, props off, motors stopped).

| Result | Meaning |
| --- | --- |
| `WHO_AM_I` is 0x70 | An MPU6500: no compass. Use a GY-91 with a genuine MPU9250, or the MPU6050 guide with the compass off |
| `WHO_AM_I` not 0x70 / 0x71 / 0x73 | ArduPilot won't detect this chip. Use another module |
| Compass `NOT found` with `WHO_AM_I` 0x71/0x73 | Damaged or fake chip: use another module |
| Reads fail, or 0x68 / 0x76 missing from the scan | Wiring, pins that don't match the sketch, or the mode pins (1.2) |
| BAD on the towel | **The chip itself is noisy**: replace the GY-91 |
| GOOD on the towel, BAD on the frame | The frame or mount shakes it: firmer foam, no wires tugging |
| GOOD until the battery goes in | Electrical noise from the ESCs or BEC: separate the GY-91 wires from power wires, add a capacitor across 5V |
| GOOD in all three | **The GY-91 is fine.** |

**Check:** `WHO_AM_I` 0x71 or 0x73, the compass answers with `WIA = 0x48`, and GOOD (or at least OK) in all three runs.

## Part 3: Build environment (once)

**Already built an ESP32 ArduPilot board on this PC?** Skip to Part 4: the same `~/ardupilot_esp32_build/ardupilot` checkout and `get_idf` shortcut build this board too.

Use **Ubuntu 22.04 or newer** (native, or WSL2 on Windows). Check with `lsb_release -a`. Run these in order.

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

git checkout 9dd2e0f3                               # optional: the exact tested commit

git submodule update --init --recursive

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

**Check:** `get_idf` ends with `Done!`, and `which python3` now points into `~/.espressif/python_env/`. Run `get_idf` in every new terminal before building. `git describe --always` in the ardupilot folder shows which commit you have.

## Part 4: Board files and the ESP32 fixes

ArduPilot builds a board from two files in its own folder: `hwdef.dat` (pins and sensors) and `defaults.parm` (parameters baked into the firmware, applied to every parameter you haven't saved yet). This Part creates both for `esp32lukasgy91`, then applies the source fixes. Every step is safe to run again.

### 4.1 Create the board folder and `hwdef.dat`

Change `WIFI_SSID` and `WIFI_PWD` (8+ characters) before you run it. If you wired different I2C pins, change the `ESP32_I2CBUS` line too.

```bash
cd ~/ardupilot_esp32_build/ardupilot

mkdir -p libraries/AP_HAL_ESP32/hwdef/esp32lukasgy91

cat > libraries/AP_HAL_ESP32/hwdef/esp32lukasgy91/hwdef.dat <<'EOF'
# esp32lukasgy91: WEMOS D1 Mini ESP32 (or DevKit) + GY-91 on I2C (MPU9250 IMU, BMP280 baro, AK8963 compass), indoor MTF-01 build
define HAL_ESP32_BOARD_NAME "esp32-lukas-gy91"

# --- I2C bus 0: SDA IO23, SCL IO18 = the tested wiring (any free non-boot pins work if the wiring matches; NOT IO12)
#             Port       SDA         SCL          SPEED    INTERNAL SOFT
ESP32_I2CBUS  I2C_NUM_0  GPIO_NUM_23 GPIO_NUM_18  400*KHZ  false    false

# --- IMU: the GY-91's MPU9250 at I2C address 0x68 (SDO/SAO low)
define HAL_INS_DEFAULT HAL_INS_MPU60XX_I2C
IMU Invensense I2C:0:0x68 ROTATION_NONE
define INS_MAX_INSTANCES 3

# --- Barometer: the GY-91's BMP280 at 0x76 (SDO/SAO low)
BARO BMP280 I2C:0:0x76
define HAL_BARO_ALLOW_INIT_NO_BARO 1

# --- Compass: the AK8963 inside the MPU9250, at 0x0C once the IMU driver turns on I2C bypass.
#     probe_mpu9250 starts the IMU first, so the bypass is open before the compass is searched for
define AP_COMPASS_PROBING_ENABLED 1
COMPASS AK8963:probe_mpu9250 I2C:0:0x0C ROTATION_NONE
define AP_COMPASS_AK8963_ENABLED 1

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

# --- SD card / logging (the MINI32 has no card slot; the boot-time SD errors are harmless)
define AP_FILESYSTEM_ESP32_ENABLED 1
define HAL_ESP32_SDMMC 1
define HAL_ESP32_SDCARD 1
define LOGGER_MAVLINK_SUPPORT 1
define HAL_LOGGING_BACKENDS_DEFAULT 1

# --- optical flow (MTF-01): needs Fix 1 (4.3)
define AP_OPTICALFLOW_ENABLED 1
EOF
```

**What the sensor lines do.** `IMU Invensense I2C:0:0x68` looks for the MPU9250 on I2C bus 0 at 0x68; when it finds one on I2C, the driver turns on the chip's I2C bypass (`AP_InertialSensor_Invensense.cpp`: "Enable I2C bypass to access internal device"). `BARO BMP280 I2C:0:0x76` finds the barometer. The compass line is **fix 5**: ArduPilot searches for compasses before it starts the IMU, so a plain `COMPASS AK8963 I2C:...` line would knock while the bypass is still shut. The `probe_mpu9250` form starts the IMU first (`AP_Compass_AK8963.cpp`). `ROTATION_NONE` is right for the compass too: its axes differ from the gyro's, but the IMU driver's axis mapping already lines them up.

### 4.2 Create `defaults.parm`

```bash
cat > ~/ardupilot_esp32_build/ardupilot/libraries/AP_HAL_ESP32/hwdef/esp32lukasgy91/defaults.parm <<'EOF'
# esp32lukasgy91 defaults (GY-91 with compass + MTF-01, indoor). Applied to every never-saved parameter.

# --- loop rate: the ESP32 cannot run 400 Hz
SCHED_LOOP_RATE 150

# --- frame: quad X, plain PWM ESCs
FRAME_CLASS 1
FRAME_TYPE 1
MOT_PWM_TYPE 0
MOT_PWM_MIN 1000
MOT_PWM_MAX 2000

# --- PWM rate: 50 Hz suits plane-style ESCs (and their calibration)
RC_SPEED 50

# --- flight modes on channel 5: Stabilize / AltHold / AltHold (Loiter added in Part 8)
FLTMODE_CH 5
FLTMODE1 0
FLTMODE2 0
FLTMODE3 0
FLTMODE4 2
FLTMODE5 2
FLTMODE6 2

# --- channel 6 = arm (up) / disarm (down); after boot flip it down, then up
RC6_OPTION 153

# --- normal start-up; Mission Planner's Calibrate ESCs works with Fix 4 (Part 7)
ESC_CALIBRATION 0

# --- logging off: no SD card on the MINI32 (set 1 once you fit one)
LOG_BACKEND_TYPE 0

# --- failsafes
FS_THR_ENABLE 3
FS_THR_VALUE 975
FS_GCS_ENABLE 0
FS_EKF_ACTION 1

# --- compass: the GY-91's AK8963 (declination left at the defaults)
COMPASS_ENABLE 1

# --- MTF-01 on SERIAL3 (IO16 RX / IO17 TX)
SERIAL3_PROTOCOL 1
SERIAL3_BAUD 115
FLOW_TYPE 5
RNGFND1_TYPE 10
RNGFND1_MIN 0.01
RNGFND1_MAX 8
RNGFND1_ORIENT 25

# --- EKF3 sources for INDOOR flight: flow velocity, rangefinder height, compass yaw
EK3_SRC_OPTIONS 0
EK3_SRC1_POSXY 0
EK3_SRC1_VELXY 5
EK3_SRC1_POSZ 2
EK3_SRC1_VELZ 0
EK3_SRC1_YAW 1
EOF
```

**Why these values.** Height comes from the MTF-01's rangefinder (`EK3_SRC1_POSZ 2`); if it stops sending for more than half a second, the EKF switches to the BMP280 and back again (`AP_NavEKF3_PosVelFusion.cpp`). Heading comes from the compass (`EK3_SRC1_YAW 1`). Declination stays at ArduPilot's defaults, as flown: `COMPASS_AUTODEC` is 1, but with no GPS the flight controller has no location, so it never runs and `COMPASS_DEC` stays 0 (`AP_Compass.cpp`, `try_set_initial_location()`). Indoors this doesn't matter: the optical flow and the compass work in the same heading frame, so a small fixed offset from true north doesn't move anything. If you ever set an EKF origin in Mission Planner, auto-declination then fills in the correct value by itself. `RC_SPEED 50` is the rate the Mission Planner ESC calibration was tested at with these plane-style ESCs.

### 4.3 Fix 1: turn optical flow back on

ArduPilot's `libraries/AP_HAL/board/esp32.h` forces `AP_OPTICALFLOW_ENABLED 0` for every ESP32 board, after your board file is read. This wraps it so your board's `define AP_OPTICALFLOW_ENABLED 1` wins.

```bash
cd ~/ardupilot_esp32_build/ardupilot

F=libraries/AP_HAL/board/esp32.h

grep -q "#ifndef AP_OPTICALFLOW_ENABLED" $F || \
  sed -i 's/^#define AP_OPTICALFLOW_ENABLED 0$/#ifndef AP_OPTICALFLOW_ENABLED\n#define AP_OPTICALFLOW_ENABLED 0\n#endif/' $F

grep -n -B1 -A1 "AP_OPTICALFLOW_ENABLED" $F
```

**Check:** three lines in a row: `#ifndef AP_OPTICALFLOW_ENABLED`, `#define AP_OPTICALFLOW_ENABLED 0`, `#endif`.

### 4.4 Fix 2: motors stuck at idle

The pre-takeoff check that releases the motors after arming (`ArduCopter/takeoff_check.cpp`) is wrapped in `#if HAL_WITH_ESC_TELEM`, which is 0 on every ESP32 board, so it compiles to an empty function and the motors stay at `MOT_SPIN_MIN`. This keeps only the ESC-telemetry parts conditional.

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

**Check:** it prints `fixed` (or `already fixed - nothing to do`), and the grep shows `#if FRAME_CONFIG != HELI_FRAME`, two `#if HAL_WITH_ESC_TELEM`, one `#else` and `// no ESC telemetry: nothing to check`. If it stops with `file differs from expected`, ArduPilot has probably merged its own fix: check the file no longer starts with `#if HAL_WITH_ESC_TELEM && FRAME_CONFIG != HELI_FRAME`, then skip this step.

### 4.5 Fix 3: let the I2C IMU reader run on time

On ESP32 every bus has its own task with a priority (higher runs first). In `libraries/AP_HAL_ESP32/Scheduler.h` the SPI bus gets 24, the same as the main flight loop, but the I2C bus gets **5**, below the UART, timer, WiFi and motor tasks; the comment on that line says an I2C IMU needs it higher. The IMU reader then keeps getting pushed back: the main loop waits for samples, and the driver throws samples away when more than 4 pile up (`AP_InertialSensor_Invensense.cpp`). You see slow parameter loading and a laggy HUD. This sets it to 24.

```bash
cd ~/ardupilot_esp32_build/ardupilot

sed -i 's/I2C_PRIORITY = 5;/I2C_PRIORITY = 24;/' libraries/AP_HAL_ESP32/Scheduler.h

grep -n "I2C_PRIORITY = 24" libraries/AP_HAL_ESP32/Scheduler.h
```

**Check:** the grep prints exactly one line (line 78 in the tested version).

### 4.6 Fix 4: ESC calibration from Mission Planner

Mission Planner's **Calibrate ESCs** sets `ESC_CALIBRATION 3`. At the next power-up ArduPilot clears the flag (`esc_calibration_setup()`), then sends full throttle for 5 s and minimum until you unplug. On ESP32 the tasks that save parameters and write flash only start after startup ends (`Scheduler.cpp`: `_io_thread`, `_storage_thread`), and calibration never ends startup, so the cleared flag never reaches flash and every power-up calibrates again. This saves the flag and writes flash right there, on ESP32 only.

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

grep -n "ESP32 FIX" ArduCopter/esc_calibration.cpp
```

**Check:** it prints `fixed` (or `already fixed - nothing to do`), and the grep shows one line. In the ESP32 emulator, with `ESC_CALIBRATION 3`, the first boot entered calibration and the reboot on the same flash read `ESC_CALIBRATION 0` and started normally; on the bench, Mission Planner's calibration completed and the board booted normally afterwards.

### 4.7 Check the parameter names

A misspelled name in `defaults.parm` is silently ignored, so check them against ArduCopter's parameter list:

```bash
cd ~/ardupilot_esp32_build/ardupilot/Tools/autotest/param_metadata

python3 param_parse.py --vehicle ArduCopter --format json > /dev/null   # creates apm.pdef.json

cd ~/ardupilot_esp32_build/ardupilot

python3 - <<'EOF'
import json
d = json.load(open('Tools/autotest/param_metadata/apm.pdef.json'))
names = {k for grp in d.values() for k in grp}
bad = [l.split()[0] for l in open('libraries/AP_HAL_ESP32/hwdef/esp32lukasgy91/defaults.parm')
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

./waf configure --board=esp32lukasgy91        # choose this board

./waf copter                                  # build ArduCopter (about 10 minutes the first time)
```

**Check:** it ends with `'copter' finished successfully` and an `ardupilot.bin binary size` line showing about 34% free. If `waf` stops at `esp-idf_showinc` with `constraints ... doesn't exist`, run `export IDF_PYTHON_CHECK_CONSTRAINTS=no`, delete `build/esp32lukasgy91/esp-idf_build`, and build again.

### 5.2 Confirm the sensors and all fixes are inside

Do this before every flash that follows a board-file or source change.

```bash
cd ~/ardupilot_esp32_build/ardupilot

grep -o "\.sda=GPIO_NUM_[0-9]*, \.scl=GPIO_NUM_[0-9]*" build/esp32lukasgy91/hwdef.h   # your I2C pins

grep "HAL_INS_PROBE1" build/esp32lukasgy91/hwdef.h                                    # IMU on I2C bus 0, 0x68

grep -o "AK8963::probe_mpu9250([^;]*" build/esp32lukasgy91/hwdef.h                     # compass

nm -C build/esp32lukasgy91/esp-idf_build/ardupilot.elf | grep -c AP_OpticalFlow_MAV   # Fix 1

nm -C -S build/esp32lukasgy91/esp-idf_build/ardupilot.elf | grep " Copter::takeoff_check()"   # Fix 2

grep -c "I2C_PRIORITY = 24" libraries/AP_HAL_ESP32/Scheduler.h                       # Fix 3

grep -c "ESP32 FIX" ArduCopter/esc_calibration.cpp                                    # Fix 4

grep -c "defaults.parm" build/esp32lukasgy91/ap_romfs_embedded.h                      # defaults embedded
```

**Check** (these are the values the test build showed):

| Line | Must show |
| --- | --- |
| I2C pins | `.sda=GPIO_NUM_23, .scl=GPIO_NUM_18` (or the pins you wired) |
| `HAL_INS_PROBE1` | `AP_InertialSensor_Invensense::probe(*this,GET_I2C_DEVICE(0,0x68),ROTATION_NONE)` |
| Compass | `AK8963::probe_mpu9250(GET_I2C_DEVICE(0,0x0c),ROTATION_NONE)` |
| `AP_OpticalFlow_MAV` | A number above 0 (the test build: 8). A 0 means 4.3 didn't run |
| `Copter::takeoff_check()` | A size other than `00000005` (the test build: `000000a4`). `00000005` means 4.4 didn't run |
| `I2C_PRIORITY = 24` | 1. A 0 means 4.5 didn't run |
| `ESP32 FIX` | 1. A 0 means 4.6 didn't run |
| `defaults.parm` | 1 |

If you change a source file or a board file, run `./waf copter` again before flashing. After a board-file change, run `./waf configure --board=esp32lukasgy91` first.

### 5.3 Flash

Flash the **bare board** (nothing but USB connected). The first flash of this board must erase the flash, so `defaults.parm` applies to every parameter. Close Mission Planner and any serial monitor first: only one program can hold the port.

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

**Check:** it ends with `Hash of data verified` and `Hard resetting`. For later flashes, skip `erase_flash`: your parameters and calibrations stay. To get the Windows COM port back for Mission Planner: `usbipd detach --busid 1-1`.

**If flashing won't start** (`Failed to connect`), put the board in download mode by hand: jumper **IO0 to GND**, press and release **RST**, then:

```bash
cd ~/ardupilot_esp32_build/ardupilot/build/esp32lukasgy91/esp-idf_build

esptool.py --chip esp32 -p /dev/ttyUSB0 --before no_reset erase_flash

# press and release RST again (jumper still in), then:
esptool.py --chip esp32 -p /dev/ttyUSB0 --before no_reset -b 460800 write_flash @flash_args
```

Remove the jumper and press RST to start the firmware.

### 5.4 Check that it boots

Open a serial monitor at 115200 (Arduino IDE, or `python3 -m serial.tools.miniterm /dev/ttyUSB0 115200` in Ubuntu, quit with Ctrl+\]) and press **RST**.

| In the log | Meaning |
| --- | --- |
| `rst:0x1 (POWERON_RESET)` at the top | Normal |
| `Init ArduCopter V4.8.0-dev (9dd2e0f3)` | ArduPilot started (your commit in brackets) |
| `Firmware change: erasing EEPROM...` | Normal on the first boot after `erase_flash` only |
| `sdmmc_...` errors, `sdcard is not mounted` | Harmless: the MINI32 has no SD card |
| `loop_rate: actual: ...Hz, expected: 150Hz` every 10 s | **Startup finished**, the main loop runs |
| `Config Error: INS: unable to initialise driver` every 5 s | IMU not found: wiring or I2C pins (1.2) |
| A reset that repeats (`rst:0xc`, `0x7`, `0x8`, `0xf`) | Crash, watchdog or brownout: see Troubleshooting |

**The first boot after `erase_flash` is the slowest.** ArduPilot saves every parameter for the first time, and on ESP32 that waits until startup has finished; in the ESP32 emulator it took about a minute. Give it that minute without pressing RST or reopening the port, because a reset starts it over. Later boots are quick.

**Check:** the `loop_rate` line appears and the log stops resetting. On the next power-up, `Firmware change: erasing EEPROM` must be gone (proof that parameters are saved).

## Part 6: Mission Planner setup and calibration

Props **off** for this whole Part. Reboot the board (press RST) after any group of parameter changes.

### 6.1 Connect

- **USB (best for setup):** pick the board's COM port and **115200** (not AUTO). In **Config → Planner**, **untick Reset on USB connect (DTR)**: with it ticked, every connect restarts the ESP32 and it has to boot all over again.
- **WiFi (good for watching values during tests):** join your `WIFI_SSID` network, then connect with **TCP**, host **192.168.4.1**, port **5760**.

**Check:** the HUD shows **DISARMED**, and the parameter download finishes in well under a minute (a slow download means Fix 3 is missing).

### 6.2 Set the parameters that need a reboot first

In **Config → Full Parameter List**, set and **Write Params**, then reboot:

| Parameter | Value | Why |
| --- | --- | --- |
| `MAV3_OPTIONS` | 2 | Don't forward MAVLink traffic to the MTF-01. MAV numbers count MAVLink ports in order: USB = MAV1, WiFi = MAV2, MTF-01 = MAV3. Appears after the first reboot |
| `SERIAL3_OPTIONS` | 0 | Leave at 0: the old bit 10 (1024) blocks arming on 4.7+ firmware |
| `FLOW_ORIENT_YAW` | 0 | Starting value; 6.7 tells you whether to change it |

The rest is set by `defaults.parm`. Confirm `SCHED_LOOP_RATE` = 150, `FLOW_TYPE` = 5 and `COMPASS_ENABLE` = 1; if they aren't, the flash wasn't erased (5.3).

### 6.3 Were all three sensors found?

In **Config → Full Parameter List**:

| Parameter | Good |
| --- | --- |
| `INS_ACC_ID`, `INS_GYR_ID` | Any non-zero number: the IMU was found |
| `BARO1_DEVID` | **226817** = hex `0x037601`: BMP280 (type `03`), address `0x76`, I2C bus 0. 0 means no barometer |
| `COMPASS_DEV_ID` | **265217** = hex `0x040C01`: AK8963 (type `04`), address `0x0C`, I2C bus 0. 0 means no compass |

The Messages tab after boot should also show `Barometer 1 calibration complete`.

### 6.4 Accelerometer, level and orientation

Always **power up with the drone sitting still**: ArduPilot measures the gyro's zero point in the first seconds.

In **Setup → Mandatory Hardware → Accel Calibration**: **Calibrate Accel** (six positions), then **Calibrate Level** with the frame on a level surface. **Check:** the HUD is level when the frame is level, and there is no `DCM Roll/Pitch inconsistent` message.

**Orientation check.** With the frame level, tilt the nose down: the HUD should pitch nose down. Lift the left side: the HUD should roll right. If either is wrong or swapped, the GY-91 is turned on the frame. Set `AHRS_ORIENTATION` by where its Y axis points: right = 2, backward = 4, left = 6. Reboot, check again, then redo the accelerometer calibration. The compass turns with it, because ArduPilot treats this compass as part of the board.

**IMU still check** (Data → Status, frame on a level table, USB only, 5 minutes):

| Value | Good |
| --- | --- |
| HUD roll / pitch | Steady within about ±1° |
| `ax`, `ay` | Near 0, within about ±10 milli-g |
| `az` | Near −1000 (1 g) |
| `gx`, `gy`, `gz` | Near 0, small noise, no steady swing |

To see the noise, tick **Tuning** under the map, double-click the graph and pick `ax` and `ay`: a flat band about ±10 wide is good.

### 6.5 Compass calibration and heading check

1. **Setup → Mandatory Hardware → Compass.** The compass list should show the AK8963 (the `COMPASS_DEV_ID` from 6.3) with **Use Compass 1** ticked.
2. Take the drone **away from steel, desks with metal frames and the laptop** (outdoors is best), click **Start** under Onboard Mag Calibration, and rotate the drone slowly through every axis until the bar completes.
3. When it reports success, **reboot**.
4. **Heading check:** compare the HUD heading with a phone compass, pointing the drone's nose N, E, S and W. **Check:** within about 15° at each.
5. **Field check:** in Data → Status, `magfield` stays steady as you turn the drone. It jumps near steel: that is the room, not the sensor.

If calibration keeps failing or the heading is off by 90° or 180°, recheck the orientation (6.4) first. Motor interference is measured with props on in Part 8.

### 6.6 Barometer check

| Value (Data → Status) | Good |
| --- | --- |
| `press_abs` | About 1000–1015 hPa near sea level, changing only slightly at rest |
| `press_temp` | Room temperature, a few degrees warmer is normal |
| Lift test: floor → above your head (about 1.5 m) → floor | `press_abs` drops by about 0.2 hPa and comes back |

The HUD altitude follows the MTF-01's rangefinder (`EK3_SRC1_POSZ 2`), not the barometer. This board won't warn you about a missing barometer at arming (`HAL_BARO_ALLOW_INIT_NO_BARO` skips that check), so this check is the only one you get.

### 6.7 MTF-01 checks

Configure the MTF-01 in **MicoAssistant** before connecting it: output protocol `mav_apm`, `mav_id` 200 (anything except 1), baud 115200.

**Data check** (hold the drone 30–50 cm over a textured floor, in normal room light):

| Value | Good |
| --- | --- |
| `rangefinder1` | Follows the height (about 30 at 30 cm, 50 at 50 cm) |
| `opt_qua` | Well above 0 (0 = can't track: plain floor, dim light, or the lens covered) |
| Messages | No `PreArm: Rangefinder 1: No Data` |

**Orientation (tilt) test.** `opt_m_x` and `opt_m_y` are the flow rate minus the drone's own rotation (`GCS_Common.cpp`, `send_opticalflow`), so tilting over a still floor should cancel out:

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

**Move test:** right-click the map → **Set EKF Origin Here**. Carry the drone level at about 50 cm, 1 m forward, then 1 m right. The map icon should follow and stay put when you stop, and its heading arrow should match where the nose points.

### 6.8 Radio, flight modes and arm switch

| Step | How | Check |
| --- | --- | --- |
| Radio | **Setup → Mandatory Hardware → Radio Calibration**: move every stick to its corners and every switch through every position, then **Click when Done** | Throttle moves bar 3 (about 1000 to 2000); roll, pitch, yaw on bars 1, 2, 4 |
| Flight modes | **Setup → Flight Modes**: flip the CH5 switch | The highlighted mode changes (Stabilize / AltHold) |
| Arm switch | **Data → Status → `ch6in`** while you flip CH6 | Goes **below 1200** and **above 1800**; if not, set the radio's CH6 end points to 100% |

## Part 7: ESC calibration

The ESCs must learn that 1000 µs means stop and 2000 µs means full, the range `MOT_PWM_MIN` / `MOT_PWM_MAX` send. With Fix 4 in the firmware, Mission Planner's **Calibrate ESCs** works on this board; the Arduino sketch in 7.3 is the backup.

### 7.1 Get ready (props OFF)

1. **Hardware:** all **props off**. The four ESC signal wires on IO25, IO27, IO33 and IO32, their grounds on ESP32 GND, and **one ESC's red BEC wire on the ESP32's 5V**: the battery must power the ESP32, because USB will be unplugged.
2. **Mission Planner** (USB): in **Config → Full Parameter List**, check `MOT_PWM_MIN` = 1000, `MOT_PWM_MAX` = 2000 and `RC_SPEED` = 50. **Write Params** if you changed any.
3. **Setup → Mandatory Hardware → ESC Calibration → Calibrate ESCs.** This sets `ESC_CALIBRATION` to 3 for the next power-up.

### 7.2 Calibrate (hardware only)

1. Unplug **USB**, then unplug the **battery**. Everything is off.
2. Plug in the **battery only** (no USB), touch nothing, and listen:

| Time after plugging in | You hear | Meaning |
| --- | --- | --- |
| About 1 s | ESC startup tune | ESCs powered |
| About 3–4 s | **beep-beep** | The ESP32 is sending full throttle; the ESCs store it as the maximum |
| About 5 s later | **3 short beeps, then 1 long beep** | Minimum stored: **calibrated** |

3. Unplug the battery.

**If the motors spin instead of beeping, unplug the battery at once** (that is why the props are off). Mission Planner shows nothing during calibration: on ESP32 MAVLink only starts after startup, and calibration happens inside startup.

**Check:** plug USB back in and connect Mission Planner: it connects normally, and `ESC_CALIBRATION` now reads **0**. Then **Setup → Optional Hardware → Motor Test** at 10%: every motor starts together with the others and spins smoothly; at 50% clearly faster.

### 7.3 Backup: calibrate with a sketch

Use this if Mission Planner's method doesn't work on your ESCs. It drives all four motor pins at once and tells you when to plug in the battery. Afterwards, flash ArduPilot back (5.3, without `erase_flash`).

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

### 7.4 ESC beep codes

Generic plane-style 40A ESCs:

| You hear | Meaning | Fix |
| --- | --- | --- |
| One beep about every 2 s | No signal | Signal wire on the right pin; ESC signal ground to ESP32 GND |
| Fast continuous beeping | Throttle not at minimum | Calibrate (7.2) |
| Double beeps repeating | Battery voltage out of range | Fully charged pack |

If a motor stutters or ignores higher % in Motor Test, keep `RC_SPEED` at 50; faster rates (up to 400 Hz on ESP32) only work if your ESCs accept them.

## Part 8: Bench test, then indoor flight in stages

### 8.1 Bench test (props OFF)

Work through every item; fix any failure before moving on.

- [ ] Boot log shows the `loop_rate` line and stops resetting (5.4)
- [ ] All three sensors found: `INS_ACC_ID`, `BARO1_DEVID`, `COMPASS_DEV_ID` non-zero (6.3)
- [ ] HUD level and steady within about ±1°; orientation check passes (6.4)
- [ ] Compass calibrated, HUD heading within about 15° of a phone at N, E, S, W (6.5)
- [ ] Radio bars correct; CH5 switches Stabilize / AltHold (6.8)
- [ ] Transmitter off → Mission Planner reports a radio failsafe
- [ ] MTF-01 data, tilt test and move test pass (6.7)
- [ ] ESCs calibrated; **Motor Test** at 10%: A = front-right, B = rear-right, C = rear-left, D = front-left, each spinning the way the Part 1 motor table says (swap any two of a motor's three wires to reverse it)
- [ ] **Arm:** after boot, flip CH6 **down, then up**. HUD shows **ARMED**, and **Data → Status** shows `ch1out` to `ch4out` at about **1100** with the stick down
- [ ] **Throttle:** push the stick up slowly. `ch1out` to `ch4out` rise **together** toward about 1900 and the motors speed up
- [ ] **Stabilisation:** at about 30% throttle, tilt the frame by hand; the motors on the low side speed up
- [ ] **Disarm:** CH6 down stops the motors at once

What the output numbers mean when you think it's armed:

| `ch1out`–`ch4out` with stick up | Meaning |
| --- | --- |
| Rise toward 1900 | Working |
| Stay at **1000** | **Not armed** (1000 = disarmed). Motor Test still works because it arms the motors itself. Flip CH6 down then up, and read the Arm:/PreArm: message |
| Stay at **1150** | Fix 2 (4.4) is missing from this firmware |
| Split (two high, two low) | The attitude estimate thinks the drone is tilted: redo Calibrate Level, check the GY-91 mount |

### 8.2 Props on, and compass motor interference

- Each prop matches its motor's spin direction, printed side up, with the thicker leading edge going first.
- **Tether** the drone (or hold it down firmly) for the first throttle-ups, with your finger on the CH6 disarm switch.
- **Compass/motor check:** with the drone tied down, run **Setup → Optional Hardware → Compass/Motor Calib**, raise the throttle slowly to about 50–75% and back, then finish. **Check:** interference under about 30%. Above that, move the battery and ESC power leads away from the GY-91 and repeat.
- Fly over a **textured floor** in good light, between about 0.3 and 2 m.

### 8.3 Fly in stages

| Stage | Mode | Pass when | Then |
| --- | --- | --- | --- |
| 1 | **Stabilize**, tethered, 0.5–1 m | Hovers without fighting you; in Status (over WiFi), `vibex/y/z` below about 15–20 and `clip0` not rising | Hover 1–2 minutes so `MOT_THST_HOVER` learns the hover throttle (it moves away from 0.35) |
| 2 | **AltHold**, throttle stick at **mid** | Holds height with the stick centred; climbs above mid, descends below | If it pumps up and down, check vibration (`vibez`) and `rangefinder1` |
| 3 | **Loiter** or **PosHold** | Holds position hands-off, nose stays pointing the same way | Only after 6.5 and 6.7 passed; see below |

**Stage 3 procedure:** set `FLTMODE6 5` (Loiter) or `16` (PosHold), reboot, take off in **AltHold** to about 1 m, then switch with your finger on the mode switch. If it leans, accelerates or slowly turns its nose, switch straight back to AltHold and land: a wandering heading points to the compass (8.2). ArduPilot's optical-flow guide asks for about 15 m of clear space for the first position-hold test; use the largest space you have.

**If the compass misbehaves indoors** (heading jumps near steel, or EKF yaw-reset messages), go back to a gyro-only heading: `COMPASS_ENABLE 0` and `EK3_SRC1_YAW 0`, reboot. Flow-based flying works without a compass; the heading then drifts slowly instead.

**Logs:** `LOG_BACKEND_TYPE 0` means nothing is recorded. Before tuning or chasing a misbehaving Loiter, fit an SD card (and set `LOG_BACKEND_TYPE 1`) or log over MAVLink (`LOG_BACKEND_TYPE 2`, needs a log receiver connected).

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| No MAVLink heartbeat in Mission Planner, but the WiFi network shows up; the boot log stops after the SD-card lines | Startup hasn't finished: on ESP32, MAVLink only starts once it does. Usually the IMU isn't found because the wired I2C pins differ from the hwdef, or it's the slow first boot after `erase_flash` | Wire SDA/SCL to the pins in `ESP32_I2CBUS` (1.2) and run the Part 2 sketch on those pins; after a fresh flash wait a minute (5.4); untick Reset on USB connect (6.1) |
| `Config Error: INS: unable to initialise driver` in the boot log | IMU not found at 0x68 | Part 2 sketch: the scan must show 0x68 and `WHO_AM_I` 0x71/0x73; check SDA/SCL go to the hwdef pins and aren't swapped |
| Parameters load slowly; the HUD lags or looks frozen | The I2C IMU reader runs at priority 5 | Do Fix 3 (4.5), check 5.2, build and flash. (Removing the SD-card driver doesn't fix this) |
| `COMPASS_DEV_ID` is 0 | The chip is an MPU6500 (`WHO_AM_I` 0x70) with no compass, or the compass line isn't the `probe_mpu9250` form | Part 2: `WHO_AM_I` must be 0x71/0x73 and the compass must answer `WIA = 0x48`; check the compass line in 4.1 and 5.2 |
| `PreArm: Compass not calibrated` | Compass calibration not done or not saved | Do 6.5 and reboot |
| Heading off by 90° or 180°, or compass calibration keeps failing | GY-91 turned on the frame, or steel nearby | Orientation check (6.4); calibrate away from steel |
| Heading wanders when the motors spin; Loiter/PosHold slowly turns or drifts | Motor current near the compass | Compass/motor check (8.2), move power leads away; if it stays bad: `COMPASS_ENABLE 0`, `EK3_SRC1_YAW 0` |
| No `Barometer 1 calibration complete`; `BARO1_DEVID` is 0 | BMP280 not found at 0x76 | Part 2 scan for 0x76; SDO/SAO and CSB (1.2) |
| The scan shows 0x69 or 0x77 | SDO/SAO is floating high | Tie SDO/SAO to GND |
| The scan misses 0x68 or 0x76 | NCS or CSB isn't high, so that chip isn't in I2C mode | Tie NCS and CSB to the GY-91's 3V3 pin |
| Board won't boot, or boot-loops right after wiring the GY-91 | Something with a pull-up on IO12 (boot pin) | Move it to other pins; keep IO12 free |
| `PreArm: Rangefinder 1: No Data`; ground speed and height wander while still | The EKF has lost the MTF-01 | Check MTF-01 wiring (TX → IO16, RX → IO17), MicoAssistant settings, `SERIAL3_PROTOCOL 1`, `SERIAL3_BAUD 115`, `MAV3_OPTIONS 2` |
| `FLOW_TYPE` missing from the parameter list | Flow compiled out | Do Fix 1 (4.3), configure and build again, check 5.2 |
| Armed, throttle does nothing; `ch1out`–`ch4out` stay at 1150 | Motors-stuck-at-idle bug | Do Fix 2 (4.4), rebuild, check the `takeoff_check` size (5.2), flash |
| `ch1out`–`ch4out` stay at 1000 while you think it's armed | Not armed | HUD must say ARMED; flip CH6 down then up; `ch6in` must pass 1200 and 1800 |
| Board stuck after **Calibrate ESCs**: calibrates at every power-up, Mission Planner never connects | Firmware without Fix 4 | Wipe only the parameter storage: `esptool.py --chip esp32 -p /dev/ttyUSB0 erase_region 0x310000 0x40000` (IO0 to GND if it won't connect), flash firmware with Fix 4 (4.6), redo Part 6 |
| Motors spin up during ESC calibration instead of beeping | The ESCs saw a low signal before full throttle | Unplug the battery at once; use the sketch (7.3) |
| ESC beeps once every 2 s | No PWM signal | Signal wire on the right pin; ESC signal ground to ESP32 GND |
| Motor Test works but motors stutter or ignore higher % | ESC can't follow the PWM rate | `RC_SPEED 50` (7.4) |
| AltHold pumps up and down | Vibration, wrong hover throttle, or noisy rangefinder | `vibez` below about 15–20; hover longer so `MOT_THST_HOVER` learns; check `rangefinder1` |
| Loiter leans hard or flips | Flow orientation inverted, or a bad attitude or heading estimate | Back to AltHold; redo the tilt test (6.7); fix `FLOW_ORIENT_YAW`; check the heading (6.5) |
| Board resets when the throttle goes up (`rst:0xf`) | 5V brownout | Stronger 5V supply, capacitor across 5V and GND |
| `waf` fails with `constraints ... doesn't exist` | ESP-IDF Python check | `export IDF_PYTHON_CHECK_CONSTRAINTS=no`, delete `build/esp32lukasgy91/esp-idf_build`, build again |

## Sources

**ArduPilot documentation and pull requests**

- [Electronic Speed Controller (ESC) Calibration](https://ardupilot.org/copter/docs/esc-calibration.html)
- [Optical flow sensor setup](https://ardupilot.org/copter/docs/common-optical-flow-sensor-setup.html)
- [MicoAir MTF-01](https://ardupilot.org/copter/docs/common-mtf-01.html)
- [AP\_HAL\_ESP32 README](https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_HAL_ESP32/README.md)
- [ArduPilot PR #34070](https://github.com/ArduPilot/ardupilot/pull/34070) and [PR #34132](https://github.com/ArduPilot/ardupilot/pull/34132): the motors-stuck-at-idle bug

**Forum threads**

- [ESC\_CALIBRATION = 3: Auto Calibrate Clarifications](https://discuss.ardupilot.org/t/esc-calibration-3-auto-calibrate-clarifications/131702)
- [450mm ESP32 Quadcopter Success](https://discuss.ardupilot.org/t/450mm-esp32-quadcopter-success/142049): a GY-91 on I2C flying on an ESP32
- [Copter without GPS AND Compass](https://discuss.ardupilot.org/t/copter-without-gps-and-compass-re-ask/139756)

**ArduPilot source files checked (master, October 2026)**

- `libraries/AP_InertialSensor/AP_InertialSensor_Invensense.cpp`: WHO\_AM\_I list, axis mapping (body X = chip Y, body Y = chip X, body Z = −chip Z), I2C bypass for the MPU9250, FIFO reset after 4 samples on I2C
- `libraries/AP_Compass/AP_Compass_AK8963.cpp` (`probe_mpu9250`): starts the IMU before probing the AK8963
- `libraries/AP_HAL_ESP32/Scheduler.h`: task priorities (`I2C_PRIORITY = 5`, `SPI_PRIORITY = 24`, `MAIN_PRIO = 24`)
- `libraries/AP_HAL_ESP32/Scheduler.cpp` (`_uart_thread`, `_io_thread`, `_storage_thread`): wait until startup has finished
- `libraries/AP_HAL_ESP32/Storage.cpp`: flash is written one line per `_timer_tick()`
- `ArduCopter/esc_calibration.cpp`: `esc_calibration_setup()` and `esc_calibration_auto()`
- `ArduCopter/takeoff_check.cpp` and `libraries/AP_Motors/AP_MotorsMulticopter.cpp`: the spool-up block
- `libraries/AP_HAL/board/esp32.h`: optical flow off by default
- `libraries/AP_NavEKF3/AP_NavEKF3_PosVelFusion.cpp`: falls back to the barometer when the rangefinder stops
- `libraries/AP_Baro/AP_Baro_BMP280.cpp` and `libraries/AP_Arming/AP_Arming.cpp`: BMP280 IDs; barometer arming check skipped with `HAL_BARO_ALLOW_INIT_NO_BARO`
- `libraries/GCS_MAVLink/GCS_Common.cpp` (`send_opticalflow`): what `opt_m_x/y` mean

**Datasheet**

- InvenSense MPU-9250 Product Specification: accel noise 300 µg/√Hz, gyro noise 0.01 °/s/√Hz; AK8963 WIA = 0x48
