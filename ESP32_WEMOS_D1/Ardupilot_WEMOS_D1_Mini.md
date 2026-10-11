# ArduPilot on WEMOS D1 Mini ESP32 (MINI32) — Complete Build Guide

Oct 9, 2026 · @lukas

The MINI32 carries the same ESP32-WROOM-32 chip and 4 MB flash as the DevKit V1 and breaks out every GPIO the `esp32lukas` board uses, so the same firmware runs on it; only the wiring, USB driver and power change.

> **Status and safety.** ArduPilot on ESP32 is experimental: ArduPilot's own docs say so, and no commercial ESP32 autopilot exists. Keep props **off** until Part 13, fly tethered first, and never near people. Pin numbers and ports were checked against ArduPilot master's `esp32buzz` board file (October 2026) and the MINI32 pinout.

## How to use this guide

Work through the Parts **in order**; each step ends with a **Check**, and you don't move on until it passes. Text after `#` in a command block explains that line; the shell ignores it. Inside a block, commands are separated by a blank line: copy and run one group at a time. A line ending in a backslash continues on the next line, so a group of lines joined that way is one command, and a comment line on its own describes the command below it.

**The whole job, in order.** Each line is one Part; tick it off before starting the next.

1. Check the board is a classic ESP32 with 4 MB flash (Part 1).
2. Set up Ubuntu and ESP-IDF once (Part 2).
3. Create the board file, **turn optical flow back on**, and write `defaults.parm` (Part 3).
4. Build the firmware and confirm flow and the compass are inside it (Part 4).
5. Flash the bare board and read its boot log (Part 5).
6. Connect Mission Planner over USB and prove the board is stable (Part 6).
7. Wire the GY-91, receiver, MTF-01 and motors one at a time (Parts 7–9).
8. Set parameters, calibrate, bench-test, then hover tethered (Parts 10–13).

## What changes from the DevKit build

The MINI32 is the same chip in a smaller body. Think of it as moving house with the same furniture: the firmware, board file and parameters move unchanged; only where things plug in, and how the board gets power, change.

| Item | ESP32 DevKit V1 | WEMOS D1 Mini ESP32 (MINI32) | What you do |
| --- | --- | --- | --- |
| Module | ESP32-WROOM-32 | ESP32-WROOM-32 | Nothing |
| Flash | 4 MB | 4 MB | Nothing |
| Firmware / board file | `esp32lukas` | `esp32lukas` (same file) | Same board file, plus the optical-flow fix (3.2) |
| GPIOs used | 1, 3, 4, 5, 16–19, 23, 25–27, 32, 33 | All broken out | Wire by GPIO number (Part 7) |
| Pin layout | Two single rows | Two **double** rows, some pins labelled with JTAG names (`TDI`, `TCK`…) | Use the label decoder in Part 7 |
| USB chip | CP2102 or CH340 | CP2104, CH9102 or CH340C, depending on the batch | Driver and port name differ (Part 5) |
| Buttons | EN + BOOT | Usually **RST only** | Manual flash mode uses a jumper on IO0 (Part 5) |
| Power in | VIN pin | `5V`/`VCC` pin, tied to USB 5V | Same rule: USB or battery, not both (Part 8) |

**One extra step this build needs:** ArduPilot's ESP32 code switches optical flow off for every ESP32 board, so the MTF-01 only works after the two changes in 3.2.

**Already set up the build environment for the DevKit?** Skip Part 2, keep your board folder, run 3.2 (older builds had optical flow switched off), and redo Part 4. Then flash the new files from `build/esp32lukas/esp-idf_build/` (Part 5), then continue from Part 6.

# Section 1: Install, build and flash

By the end of this section ArduPilot is built on your Linux machine, flashed onto the bare MINI32, and proven not to crash or reset.

## Part 1: Choose and check the board

### 1.1 Parts list

| Part | Role | Connects over |
| --- | --- | --- |
| WEMOS D1 Mini ESP32 (MINI32, ESP32-WROOM-32) | Flight controller | — |
| GY-91 (MPU9250 + BMP280) | IMU, compass and barometer | SPI |
| FlySky FS-A8S + FS-i6 transmitter | Pilot input | PPM (one wire) |
| MicoAir MTF-01 | Optical flow + downward rangefinder | UART (MAVLink) |
| 4 brushless ESCs + motors, **or** 4 MOSFET drivers + brushed motors (e.g. Tello 8520) | Propulsion | PWM |
| 5V BEC (brushless, 2S+) **or** 1S-to-5V boost converter (brushed, 1S) | Clean 5V supply | — |
| 3.3V USB-to-TTL adapter (CP2102/CH340/FTDI) | Configuring the MTF-01 | — |

### 1.2 What ArduPilot expects from the board

| Item | Requirement | Why |
| --- | --- | --- |
| Chip | Classic ESP32 | ArduPilot's `esp32buzz` board targets it |
| Module | **WROOM**-32 (the metal shield says so) | **Not WROVER**: WROVER uses GPIO16/17 for its PSRAM, and those are the MTF-01's pins |
| Flash | 4 MB or more | Firmware (3 MB) plus parameter storage (256 KB) need about 3.3 MB |
| Pins broken out | 1, 3, 4, 5, 16–19, 23, 25–27, 32, 33 | The MINI32 has all of them |

Avoid ESP32-C3, -C6, -S2 boards, and "D1 Mini" boards that carry an **ESP8266** instead of an ESP32: ArduPilot doesn't run on them. Some shop listings mix both in one title; the shield must say ESP32.

### 1.3 Check your board

After Part 2 is set up, plug the board in and run (use `/dev/ttyACM0` if that's the port Part 5 finds):

```bash
get_idf                                                  # load the ESP32 tools

esptool.py --chip esp32 -p /dev/ttyUSB0 flash_id         # ask the chip about itself
```

**Check:** the output includes `Chip is ESP32-D0WD...` (classic ESP32) and `Detected flash size: 4MB` (or more).

### 1.4 How ArduPilot uses the 4 MB flash

| Address | Size | Contents |
| --- | --- | --- |
| 0x1000 | \~28 KB | Bootloader |
| 0x8000 | 4 KB | Partition table |
| 0x9000 | 24 KB | NVS (ESP-IDF settings, including WiFi) |
| 0xF000 | 4 KB | Radio calibration |
| 0x10000 | 3 MB | ArduCopter firmware |
| 0x310000 | 256 KB | ArduPilot storage: **your parameters and calibration** |

Normal re-flashing leaves the storage area alone, so calibration survives firmware updates. `erase_flash` wipes everything, including storage.

## Part 2: Set up the build environment

The build runs in **Ubuntu 22.04 or newer** (Python 3.10+), natively or in WSL2 on Windows. Check with `lsb_release -a`; Ubuntu 20.04's Python 3.8 is too old for ArduPilot and isn't covered here.

Four rules:

1. **Stay on the Linux filesystem.** `pwd` must start with `/home/`, never `/mnt/c/`.
2. **Open a fresh terminal when a step says so.** Old environments linger and make the wrong Python get picked.
3. **Never hard-code `IDF_PATH` or `IDF_PYTHON_ENV_PATH`** in `~/.bashrc`.
4. **Never change the system `python3`** with `update-alternatives`; it breaks apt.

### 2.1 System packages

```bash
sudo apt update

sudo apt install -y git wget flex bison gperf cmake ninja-build ccache \
  libffi-dev libssl-dev dfu-util libusb-1.0-0 python3 python3-pip python3-venv
```

**Check:** `python3 -m venv --help > /dev/null && echo "venv OK"` prints `venv OK`.

### 2.2 Clone ArduPilot and install its prerequisites

```bash
mkdir -p ~/ardupilot_esp32_build && cd ~/ardupilot_esp32_build

git clone --recursive https://github.com/ArduPilot/ardupilot.git    # several GB

cd ardupilot

git submodule status | grep -E '^[-+U]'             # must print nothing

Tools/environment_install/install-prereqs-ubuntu.sh -y
```

Then **close the terminal and open a new one**. If the clone was interrupted, delete the `ardupilot` folder and clone again.

### 2.3 Fetch ESP-IDF 5.3

```bash
cd ~/ardupilot_esp32_build/ardupilot

./Tools/scripts/esp32_get_idf.sh

git -C modules/esp_idf describe --tags               # must show v5.3.x

git -C modules/esp_idf submodule status --recursive | grep -E '^[-+U]'   # must print nothing
```

If the last line prints anything, repair and check again:

```bash
cd modules/esp_idf

git submodule foreach --recursive git reset --hard

git submodule foreach --recursive git clean -xfd

git submodule update --init --recursive --force

cd ../..
```

### 2.4 Create the ESP-IDF Python environment

In a **fresh terminal**:

```bash
which python3                                        # must be /usr/bin/python3

echo "VIRTUAL_ENV=$VIRTUAL_ENV"                      # must be empty

cd ~/ardupilot_esp32_build/ardupilot

./modules/esp_idf/install.sh                         # ends with "All done!"
```

### 2.5 The `get_idf` shortcut

Every new terminal needs the ESP32 tools loaded before building. Add **one** line to the end of `~/.bashrc`, then open a new terminal:

```bash
alias get_idf='source ~/ardupilot_esp32_build/ardupilot/modules/esp_idf/export.sh'
```

Then, once, add ArduPilot's own Python modules to the same environment:

```bash
cd ~/ardupilot_esp32_build/ardupilot

get_idf                                              # load the tools

python3 -m pip install empy==3.3.4 pexpect           # once per environment
```

**Check:** `get_idf` prints `Python requirements are satisfied.` and ends with `Done!`; `which python3` points into `~/.espressif/python_env/idf5.3_py3.x_env/bin/`; and `python3 -c "import esp_idf_monitor, em, pexpect; print('OK')"` prints `OK`.

Ignore three things in the `get_idf` output: the cmake "recommended 3.24" warning, the "run idf.py build" hint (ArduPilot always builds with `./waf`), and paths listed twice.

## Part 3: Create your board definition

The board definition (`hwdef.dat`) is the wiring diagram you hand ArduPilot: it says which pin does what. The MINI32 uses the same GPIO numbers as the DevKit, so both use one board, `esp32lukas`. You make it in two steps: copy ArduPilot's `esp32buzz` board with your name, WiFi and compass (3.1), then switch optical flow on (3.2).

### 3.1 Create the board file

This copies `esp32buzz` and makes exactly three changes: name, WiFi, compass. Put your own WiFi name and a password of at least 8 characters, used for nothing else (the boot log prints it).

**Already have an `esp32lukas` folder** (for example with your own WiFi name)? Skip this command: it would overwrite your file. Go to 3.2, which updates an existing file safely.

```bash
cd ~/ardupilot_esp32_build/ardupilot/libraries/AP_HAL_ESP32/hwdef

rm -rf esp32lukas && mkdir esp32lukas                      # start clean

sed -e 's/"esp32-buzz"/"esp32-lukas"/' \
    -e 's/^define WIFI_SSID .*/define WIFI_SSID "lukas-drone"/' \
    -e 's/^define WIFI_PWD .*/define WIFI_PWD "lukasdrone123"/' \
    -e 's/^define AP_COMPASS_PROBING_ENABLED 1/COMPASS AK8963:probe_mpu9250 0 ROTATION_NONE\ndefine AP_COMPASS_AK8963_ENABLED TRUE\ndefine AP_COMPASS_PROBING_ENABLED 1/' \
    esp32buzz/hwdef.dat > esp32lukas/hwdef.dat               # copy with the 3 changes

diff esp32buzz/hwdef.dat esp32lukas/hwdef.dat               # show exactly what changed
```

**Check:** `diff` shows only the board name, the two WiFi lines, and two added compass lines:

```
< define HAL_ESP32_BOARD_NAME "esp32-buzz"
> define HAL_ESP32_BOARD_NAME "esp32-lukas"
> COMPASS AK8963:probe_mpu9250 0 ROTATION_NONE
> define AP_COMPASS_AK8963_ENABLED TRUE
< define WIFI_SSID "ardupilot123"
> define WIFI_SSID "lukas-drone"
...
```

### 3.2 Switch optical flow on (two changes, both required)

ArduPilot's ESP32 code turns optical flow **off for every ESP32 board** (`#define AP_OPTICALFLOW_ENABLED 0` in `libraries/AP_HAL/board/esp32.h`). Without both changes below, `FLOW_TYPE` doesn't exist and the MTF-01 can't be used.

Think of a house rule and a note on the fridge. Your board file is the note (“flow on”); `esp32.h` is the house rule (“flow off”), read after the note, so it always wins. Change 1 turns the rule into a default that a note can override; change 2 writes the note. With only one of them, flow stays off.

```bash
cd ~/ardupilot_esp32_build/ardupilot

F=libraries/AP_HAL/board/esp32.h

H=libraries/AP_HAL_ESP32/hwdef/esp32lukas/hwdef.dat

# change 1: let a board file override the ESP32 "flow off" (does nothing if already done)
grep -q "#ifndef AP_OPTICALFLOW_ENABLED" $F || \
  sed -i 's/^#define AP_OPTICALFLOW_ENABLED 0$/#ifndef AP_OPTICALFLOW_ENABLED\n#define AP_OPTICALFLOW_ENABLED 0\n#endif/' $F

# change 2: switch flow on in your board file (adds the line only if it's missing)
grep -q "^define AP_OPTICALFLOW_ENABLED 1" $H || echo "define AP_OPTICALFLOW_ENABLED 1" >> $H

# check both
grep -n -B1 -A1 "AP_OPTICALFLOW_ENABLED" $F

grep -n "AP_OPTICALFLOW_ENABLED" $H
```

**Check:**

| File | Must show |
| --- | --- |
| `esp32.h` | Three lines in a row: `#ifndef AP_OPTICALFLOW_ENABLED`, `#define AP_OPTICALFLOW_ENABLED 0`, `#endif` |
| `hwdef.dat` | Exactly one line: `define AP_OPTICALFLOW_ENABLED 1` |

- **Safe to run again.** Each change skips itself when already done, so run this block whenever you're unsure, including on an older board file.
- **Your next build must start with `./waf configure --board=esp32lukas`.** Turning flow on recompiles much of ArduPilot; a build that finishes in under a minute after this step means configure wasn't run and flow is still off. Part 4 checks the result.
- **After a `git pull`:** this edits an ArduPilot file, so run `git stash` before the pull and `git stash pop` after it, then run this block again.
- **Tested on ArduPilot master (October 2026):** with both changes the firmware contains the MTF-01's MAVLink flow driver, EKF3 flow fusion and the `FLOW_` parameters, with 33% of the firmware space still free. The ESP32 port turned flow off on purpose, probably to save space and CPU, so watch for `PreArm: Main loop slow` once the MTF-01 streams (11.0).

### 3.3 What the important lines mean

Leave these as they are; Part 7 wires the MINI32 to match them.

| hwdef line | What it sets |
| --- | --- |
| `ESP32_SPIBUS VSPI_HOST 1 GPIO_NUM_23 GPIO_NUM_19 GPIO_NUM_18` | SPI bus: MOSI 23, MISO 19, clock 18 |
| `ESP32_SPIDEV mpu9250 ... GPIO_NUM_5 ...` | MPU9250 chip select on GPIO5 |
| `ESP32_SPIDEV bmp280 ... GPIO_NUM_26 ...` | BMP280 chip select on GPIO26 |
| `COMPASS AK8963:probe_mpu9250 0 ROTATION_NONE` | Read the compass inside the MPU9250 (added by 3.1) |
| `define AP_COMPASS_AK8963_ENABLED TRUE` | Build the AK8963 driver into the firmware (added by 3.1) |
| `ESP32_I2CBUS I2C_NUM_0 GPIO_NUM_13 GPIO_NUM_12 ...` | Spare I2C bus, unused here |
| `ESP32_RCOUT GPIO_NUM_25` … | Motor outputs: GPIO25, 27, 33, 32 (plus 22, 21 spare) |
| `define HAL_ESP32_RCIN GPIO_NUM_4` | RC (PPM) input on GPIO4 |
| `ESP32_SERIAL UART_NUM_0 GPIO_NUM_3 GPIO_NUM_1` | **SERIAL0**: the USB port, for Mission Planner |
| `ESP32_SERIAL UART_NUM_1 GPIO_NUM_16 GPIO_NUM_17` | **SERIAL3**: GPIO16 (RX) / GPIO17 (TX), for the MTF-01 |
| `define HAL_ESP32_WIFI 1` | **SERIAL1**: WiFi access point, MAVLink on TCP port 5760 |
| `define AP_OPTICALFLOW_ENABLED 1` | Build optical flow (MTF-01) into the firmware; added by 3.2, together with the esp32.h change |

**Why the compass lines are needed.** The GY-91's compass sits *inside* the MPU9250 and can only be read through it. `esp32buzz` never declares it, so without these lines Mission Planner reports `Compass 1 not healthy`. ArduPilot's own `esp32nick` board declares it with exactly these two lines. Each must appear **once**; a second copy stops the build with `Error: Duplicate MAG`.

**Why the MTF-01 port is SERIAL3.** ArduPilot numbers ports in a fixed order: SERIAL0 is USB, SERIAL1 is WiFi, SERIAL2 is undefined on this board, and SERIAL3 is the second UART on GPIO16/17. The ESP32 calls those pins "uart1"; that's the building's room number, but ArduPilot delivers by flat number, and that room is flat 3.

### 3.4 Bake your parameters into the firmware (`defaults.parm`)

The build embeds a `defaults.parm` file from the board folder, and ArduPilot uses those values for every parameter that has never been saved. After an `erase_flash`, the board starts with all your settings already in place.

```bash
nano ~/ardupilot_esp32_build/ardupilot/libraries/AP_HAL_ESP32/hwdef/esp32lukas/defaults.parm
```

Paste this, save (`Ctrl+O`, `Enter`) and exit (`Ctrl+X`):

```
# --- loop rate: the ESP32 cannot run 400 Hz
SCHED_LOOP_RATE 150

# --- frame: quad X, plain PWM ESCs (brushed motors: see 11.1b)
FRAME_CLASS 1
FRAME_TYPE 1
MOT_PWM_TYPE 0
MOT_PWM_MIN 1000
MOT_PWM_MAX 2000

# --- flight modes on channel 5 (SwC): Stabilize / AltHold / Loiter
FLTMODE_CH 5
FLTMODE1 0
FLTMODE2 0
FLTMODE3 0
FLTMODE4 2
FLTMODE5 2
FLTMODE6 5

# --- channel 6 (SwA) = motor emergency stop
RC6_OPTION 31

# --- logging: the MINI32 has no SD card (set 1 if you fit one, 8.6)
LOG_BACKEND_TYPE 0

# --- failsafes
FS_THR_ENABLE 3
FS_THR_VALUE 975
FS_GCS_ENABLE 0
FS_EKF_ACTION 1

# --- MTF-01 on SERIAL3 (GPIO16 RX / GPIO17 TX)
SERIAL3_PROTOCOL 1
SERIAL3_BAUD 115
FLOW_TYPE 5
RNGFND1_TYPE 10
RNGFND1_MIN 0.01
RNGFND1_MAX 8
RNGFND1_ORIENT 25

# --- EKF3 sources for INDOOR flight (MicoAir's indoor setup, see 11.5)
# bench test without the MTF-01: set POSZ 1, VELXY 0 in Mission Planner (11.4b)
EK3_SRC_OPTIONS 0
EK3_SRC1_POSXY 0
EK3_SRC1_VELXY 5
EK3_SRC1_POSZ 2
EK3_SRC1_VELZ 0
EK3_SRC1_YAW 1
```

Don't add `SERIAL3_OPTIONS 1024`: on 4.8 firmware it blocks arming (see 11.4).

### 3.5 Check the board files before building

Two checks catch mistakes in minutes instead of after a 20-minute build. The second only proves each name exists somewhere in ArduCopter; it can't tell whether the ESP32 build includes that feature (that's how flow slipped through). Part 4 checks the built firmware itself.

**Board file:** ArduPilot's own checker must end with `Writing hwdef setup`.

```bash
cd ~/ardupilot_esp32_build/ardupilot

python3 libraries/AP_HAL_ESP32/hwdef/scripts/esp32_hwdef.py \
  libraries/AP_HAL_ESP32/hwdef/esp32lukas/hwdef.dat --outdir /tmp
```

**Parameter names:** a misspelt name in `defaults.parm` is silently ignored. This builds ArduCopter's full parameter list from the source and compares every line against it:

```bash
cd ~/ardupilot_esp32_build/ardupilot/Tools/autotest/param_metadata

python3 param_parse.py --vehicle ArduCopter --format json > /dev/null   # creates apm.pdef.json

cd ~/ardupilot_esp32_build/ardupilot

python3 - <<'EOF'
import json
d = json.load(open('Tools/autotest/param_metadata/apm.pdef.json'))
names = {k for grp in d.values() for k in grp}
bad = [l.split()[0] for l in open('libraries/AP_HAL_ESP32/hwdef/esp32lukas/defaults.parm')
       if l.split('#')[0].strip() and l.split()[0] not in names]
print("Unknown parameters:", bad if bad else "none")
EOF
```

**Check:** it prints `Unknown parameters: none`.

**Rule for future edits:** change one thing at a time, rebuild, and confirm it works before the next change.

## Part 4: Build the firmware

Build the stock `esp32buzz` board first. If that works, your toolchain is proven, and any later error comes from your own changes.

```bash
cd ~/ardupilot_esp32_build/ardupilot

get_idf                                    # load the ESP32 tools

./waf configure                            # plain configure, once on a fresh checkout

./waf configure --board=esp32buzz          # choose the stock board

./waf copter                               # build ArduCopter (10–30 min the first time)

./waf configure --board=esp32lukas         # now choose your board

./waf copter                               # build it
```

**Check:** `configure` prints `Setting board to : esp32lukas`, the build prints `Entering directory .../build/esp32lukas`, and it ends with `'copter' finished successfully`.

`./waf copter` always builds the board from the **last** `./waf configure --board=...`, like a GPS that keeps driving to the last destination you set.

- **Never use `./waf build`.** It's broken for ESP32; always use `./waf copter`.
- **Rebuild after every board-file change**, starting with `./waf configure --board=esp32lukas`.
- **If the build fails,** save the log and find the first real error: `./waf copter -j1 > build_log.txt 2>&1` then `grep -n -m3 "error" build_log.txt`.

The files you flash are in `build/esp32lukas/esp-idf_build/`: `bootloader/bootloader.bin`, `partition_table/partition-table.bin`, `ardupilot.bin`, and `flash_args` (the address for each file, so you never type addresses yourself).

**Confirm the compass, optical flow and defaults made it into the build (before every flash that follows a board-file change):**

```bash
cd ~/ardupilot_esp32_build/ardupilot

grep -c "AK8963" build/esp32lukas/hwdef.h                        # 1 or more: compass driver included

nm -C build/esp32lukas/esp-idf_build/ardupilot.elf | grep -c AP_OpticalFlow_MAV   # above 0: MTF-01 flow driver included

grep -c "defaults.parm" build/esp32lukas/ap_romfs_embedded.h     # 1: defaults file embedded

grep -o "HAL_PARAM_DEFAULTS_PATH[^ ]*" -m1 build/esp32lukas/compile_commands.json   # firmware told to load it
```

**Check:** the first three print a number above 0 (a 0 on the second means flow is still off: run 3.2 again, then configure and build again, and don't flash yet), and the fourth prints `HAL_PARAM_DEFAULTS_PATH=\"@ROMFS/defaults.parm\"`.

## Part 5: Flash the MINI32

Flash the **bare** board, with only the USB cable connected. Wired peripherals can hold boot pins in the wrong state and stop flashing.

### 5.1 Know your USB chip first

MINI32 boards ship with one of three USB chips, and each shows up under a different name. Read the small chip next to the USB socket, or check what appears when you plug in:

| USB chip | usbipd ID | Linux port | Windows shows | Windows driver |
| --- | --- | --- | --- | --- |
| CP2104 | `10c4:ea60` | `/dev/ttyUSB0` | Silicon Labs CP210x USB to UART Bridge | Silicon Labs CP210x, if not found |
| CH340C | `1a86:7523` | `/dev/ttyUSB0` | USB-SERIAL CH340 | WCH CH340/CH341, if not found |
| CH9102 | `1a86:55d4` | `/dev/ttyACM0` | USB-Enhanced-SERIAL CH9102, or USB Serial Device | WCH CH343/CH9102, if no COM port |

Example from a real MINI32: `usbipd list` shows `1-1  10c4:ea60  Silicon Labs CP210x USB to UART Bridge (COM10)`, so this board has a CP2104, appears as `/dev/ttyUSB0` in WSL and as **COM10** in Windows.

Everywhere below, use **your** port: `/dev/ttyUSB0` or `/dev/ttyACM0` in Linux, `COMx` in Windows.

Pick one flashing option:

| Option | Use it when |
| --- | --- |
| A: native Ubuntu | Ubuntu is installed directly on the PC |
| B: WSL2 + usbipd | You build in WSL2 and want to flash from Linux |
| C: Windows esptool | You build in WSL2 but would rather flash from Windows |

### Option A: native Ubuntu

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null   # find the port

sudo usermod -aG dialout $USER             # once; then log out and back in

cd ~/ardupilot_esp32_build/ardupilot

get_idf

esptool.py --chip esp32 -p /dev/ttyUSB0 erase_flash       # first flash only

ESPPORT=/dev/ttyUSB0 ESPBAUD=921600 ./waf copter --upload
```

**Check:** it ends with `Hash of data verified.` and `Hard resetting`. Every flash after the first only needs the last line (after `get_idf` in a new terminal).

### Option B: WSL2 with usbipd

WSL2 can't see USB devices by itself; usbipd lends one from Windows to Linux, and you hand it back when you're done.

**1. Attach the board to WSL.** In **PowerShell**:

```powershell
winget install --interactive --exact dorssel.usbipd-win   # once; Admin PowerShell, then reopen PowerShell

usbipd list                                               # find the board by its ID (5.1); note its BUSID, e.g. 1-1

usbipd bind --busid 1-1                                   # once per board; Admin PowerShell; state -> Shared

usbipd attach --wsl --busid 1-1                           # every time you plug in; keep Ubuntu open; state -> Attached
```

**2. Check Linux can see it, then flash.** In **Ubuntu**:

```bash
lsusb | grep -i -E "10c4|1a86"            # shows the USB chip

ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null  # ttyUSB0 (CP2104/CH340) or ttyACM0 (CH9102)
```

Now flash exactly as in Option A, with your port.

**3. Give the board back to Windows** (for Mission Planner or Option C). In **PowerShell**:

```powershell
usbipd detach --busid 1-1             # back to Windows; state -> Shared

usbipd unbind --busid 1-1             # Admin PowerShell; WSL stops claiming it; state -> Not shared

usbipd list                           # check the state; the COM port is back in Device Manager
```

Next time you flash from WSL, start again at `usbipd bind`. Leave every other device in `usbipd list` (mouse, keyboard, webcam) as **Not shared**.

### Option C: flash from Windows with esptool

The board must be in Windows' hands (detached from WSL).

**1. Copy the build files to Windows.** In **Ubuntu**:

```bash
cd ~/ardupilot_esp32_build/ardupilot/build/esp32lukas/esp-idf_build

mkdir -p /mnt/c/esp32flash                                # C:\esp32flash on Windows

cp --parents flash_args ardupilot.bin bootloader/bootloader.bin \
  partition_table/partition-table.bin /mnt/c/esp32flash/  # --parents keeps the subfolders

ls -R /mnt/c/esp32flash                                   # check all four files arrived
```

**2. Flash.** In **PowerShell**, with your COM port from Device Manager → Ports:

```powershell
winget install Python.Python.3.12                         # once, if "py --version" doesn't work; then reopen PowerShell

py -m pip install esptool                                 # once

cd C:\esp32flash

py -m esptool --chip esp32 -p COM5 erase_flash            # first flash only

py -m esptool --chip esp32 -p COM5 -b 921600 write_flash "@flash_args"
```

After every rebuild, repeat step 1 before flashing.

### If flashing won't start: manual download mode

`Failed to connect` means the board didn't enter flash mode. The ESP32 enters it when **GPIO0 is held low while it resets**. The DevKit has a BOOT button for this; most MINI32 boards have only **RST**, so use a jumper wire instead:

```bash
# 1. jumper IO0 to GND, press and release RST (board waits in flash mode)
cd ~/ardupilot_esp32_build/ardupilot/build/esp32lukas/esp-idf_build

esptool.py --chip esp32 -p /dev/ttyUSB0 --before no_reset erase_flash

# 2. press and release RST again (jumper still in)
esptool.py --chip esp32 -p /dev/ttyUSB0 --before no_reset -b 460800 write_flash @flash_args

# 3. REMOVE the jumper, then press RST to start the firmware
```

Still failing? Try another USB cable (many are charge-only) and a lower speed (`-b 115200`).

### Check that it boots

```bash
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200      # quit with Ctrl+]
```

Press **RST** and compare with a normal MINI32 boot:

| Line you see | Meaning |
| --- | --- |
| `Init ArduCopter V4.8...`, `Free RAM: ...` | ArduPilot started |
| `OK created task _main_thread` … `_storage_thread` | Its threads are running |
| `Mounting sd`, `sdmmc_card_init failed (0x107)`, `sdcard is not mounted` | No SD card fitted. **Harmless** (`0x107` = timeout) |
| `WiFi softAP init finished. SSID: ...` | WiFi network is up |
| `OK created task _wifi_thread for TCP with PORT 5760` | MAVLink over WiFi is listening |
| `DHCP server started ... 192.168.4.1` | The address Mission Planner connects to |
| `spi device constructed SPI:bmp280:0:0` | Sensor drivers starting. After this the console is normally quiet |

`Firmware change: erasing EEPROM...` is **normal on the first boot after flashing only**. The boot log prints your WiFi password, so don't reuse a password from anything else.

**Read the reset reason** on the `rst:` line at the very top of each boot. It tells you why the board restarted:

| `rst:` code | Cause |
| --- | --- |
| `0x1 (POWERON_RESET)` | Power was connected or cut: normal at plug-in |
| `0xc (SW_CPU_RESET)` + `Guru Meditation` | Firmware crash: catch it with 6.3 |
| `0xf (RTCWDT_BROWN_OUT_RESET)` | Supply voltage dipped: better 5V, add a capacitor |
| `0x7` / `0x8` | A watchdog fired: something hung |

### When to erase the flash again

Saved parameters always win over the built-in `defaults.parm`. After you change `defaults.parm`, or when old settings confuse things, run `erase_flash` before flashing. Erasing also wipes calibration, so redo Part 12 afterwards.

## Part 6: Prove the board is stable before wiring everything

A crash that restarts the board can look like a WiFi or wiring problem. Test for it now, first with the board alone, then with **only the GY-91** wired (Part 7).

### 6.1 Set Mission Planner's reset option (once)

When Mission Planner opens the COM port it can toggle the USB chip's DTR and RTS lines, which drive the ESP32's reset and GPIO0 through the MINI32's auto-reset transistors. Connecting can then reboot the board and stall. In **Config → Planner**, tick only **Reset on USB Connect (toggle DTR)** and untick RTS; if connecting is still slow, untick both.

### 6.1b If Mission Planner says “No MAVLink Heartbeat Packets”

Mission Planner waits a limited time for two heartbeats. Anything that delays them, or blocks the port, makes connecting fail some times and work others. Check in this order:

1. **Is another program holding the port?** Close every serial monitor (Arduino IDE, miniterm, `esp_idf_monitor`), and run `usbipd detach --busid <id>` if the board is attached to WSL. Only one program can open a COM port.
2. **Right port and speed?** Pick the board's COM port (e.g. COM10) and **115200**, never AUTO: at boot the ESP32 prints text at 115200 on the same line, which confuses the AUTO scan.
3. **Is connecting rebooting the board?** If the boot log appears every time you click Connect, it's the DTR/RTS reset: tick only DTR (above), or untick both. On the ArduPilot forum, both options on stopped an ESP32 connecting at all; DTR only connected in about 5 s.
4. **Is the board restarting by itself?** Check the `rst:` code (Part 5).
5. **Over WiFi:** join the drone's network, wait a few seconds for the `192.168.4.x` address, then connect TCP to `192.168.4.1` port `5760`. Untick **Connect automatically** on your home WiFi so Windows doesn't hop back.

**Check:** Connect reaches the parameter download, and the Messages tab later shows `EKF3 IMU0 initialised`, `tilt alignment complete` and `MAG0 initial yaw alignment complete`: IMU and compass are both working.

### 6.2 Tests

| Test | How | Check |
| --- | --- | --- |
| USB link | COM port, **115200**, **Connect** | Parameter download completes, HUD appears |
| Settings survive a restart | Set `FRAME_CLASS = 1`, **Write Params**, press RST, reconnect | Still `1` |
| Ten minutes on WiFi | Join `WIFI_SSID`, Mission Planner **TCP** → `192.168.4.1` port `5760` | Parameters load, link stays up |

### 6.3 If anything drops: catch the crash

A crashing ESP32 prints the reason just before it restarts. ESP-IDF's monitor translates it into ArduPilot file and function names. In Ubuntu, with the board attached to WSL:

```bash
cd ~/ardupilot_esp32_build/ardupilot

get_idf

python3 -m esp_idf_monitor -p /dev/ttyUSB0 -b 115200 \
  build/esp32lukas/esp-idf_build/ardupilot.elf             # quit with Ctrl+]
```

| What you see | Meaning | Next step |
| --- | --- | --- |
| All three tests pass | Board is stable | Carry on to Part 7 |
| `Guru Meditation Error` with decoded lines | Firmware crash | Note the file and function names; report them on the ArduPilot forum's ESP32 thread |
| `Brownout detector was triggered` | Power dip | Good 5V supply; 100–470 µF across 5V and GND |
| `Firmware change: erasing EEPROM` on every boot | Board restarting before it can save | Treat as a crash; catch it here |

# Section 2: Schematic and wiring

By the end of this section every device is wired to the right MINI32 pin and set up on its own side: the GY-91 and its compass checked, the receiver bound with its failsafe set, and the MTF-01 talking MAVLink.

## Part 7: Wire everything

Wire with the battery **disconnected** and props **off**. Add devices one at a time, checking each in Mission Planner before the next: GY-91, then receiver, then MTF-01, then motors. **Wire by GPIO number, never by position:** the MINI32's outer pin row follows the old Wemos D1 Mini layout, so pins sit in different places from the DevKit.

&#91;embedded content: MINI32 wiring · GY-91, FS-A8S, MTF-01, motors, 5V supply\]

Each line joins a device pin to the MINI32 pin with the same row; the pin table below lists the same connections with notes.

### Pin table

| Device pin | MINI32 pin | Notes |
| --- | --- | --- |
| GY-91 SCL | IO18 | Works as SPI clock |
| GY-91 SDA | IO23 | Works as SPI MOSI |
| GY-91 SDO/SAO | IO19 | Works as SPI MISO |
| GY-91 NCS | IO5 | MPU9250 chip select |
| GY-91 CSB | IO26 | BMP280 chip select |
| GY-91 VIN / GND | 3V3 / GND | Power through VIN only; leave the GY-91's own 3V3 pin unconnected |
| FS-A8S PPM | IO4 | Use the PPM pad, not iBUS or S.BUS |
| FS-A8S + / − | 5V / GND | Needs 4.0–8.4V |
| MTF-01 TX | IO16 | MINI32 RX, ArduPilot SERIAL3 |
| MTF-01 RX | IO17 | MINI32 TX |
| MTF-01 VCC / GND | 5V / GND |  |
| Motor 1–4 signal | IO25, IO27, IO33, IO32 | ESC signal or MOSFET gate (Part 8) |
| Motor signal ground | GND | Needed for a clean PWM signal |
| 5V supply | 5V (VCC) / GND | BEC or boost converter (Part 8) |

### Reading the MINI32's pin labels

Most pins are printed as `IOxx`, where `xx` is the GPIO number. A few carry the ESP32's internal names instead:

| Printed label | GPIO | Use here |
| --- | --- | --- |
| `TXD` / `RXD` | 1 / 3 | USB serial (SERIAL0); leave free |
| `SVP` / `SVN` | 36 / 39 | Input only; spare analog |
| `TDI` | 12 | Leave free: a pull-up at power-on stops the board booting |
| `TCK` | 13 | Spare I2C; leave free |
| `TMS` / `TDO` | 14 / 15 | Spare (SD card, 8.6) |
| `CMD`, `CLK`, `SD0`–`SD3` | 6–11 | Internal flash; **never connect** |
| `5V` or `VCC` | — | Tied to USB 5V; the 5V supply input |
| `3V3` | — | 3.3V output from the board's regulator |

If a pin on your board is printed differently, check it against the seller's pinout picture before wiring.

### Pins to leave alone

- **GPIO12 (`TDI`) and GPIO13 (`TCK`):** the spare I2C bus. A pull-up on GPIO12 at power-on stops the board booting.
- **GPIO0, 2, 15:** boot-strap pins. GPIO0 is also the manual flash-mode pin (Part 5).
- **GPIO6–11:** internal flash.

### Check before powering up

- **Measure the FS-A8S PPM signal** with a multimeter before connecting it to IO4. ESP32 inputs are 3.3V-only; use a resistor divider if it's higher.
- **Check for shorts** between 5V and GND before the first power-up.

### Motor layout (quad X, viewed from above, front at top)

| Motor | MINI32 pin | Position | Spin |
| --- | --- | --- | --- |
| 1 | IO25 | Front right | Counter-clockwise |
| 2 | IO27 | Rear left | Counter-clockwise |
| 3 | IO33 | Front left | Clockwise |
| 4 | IO32 | Rear right | Clockwise |

That's ArduPilot's numbering, not "1-2-3-4 around the frame"; mixing it up is the classic first-flight flip. Mount the MINI32 and GY-91 flat near the frame centre on soft foam, GY-91 arrow forward, with the MINI32's antenna end clear of metal, carbon fibre and wires.

**Where to put the GY-91.** Next to the MINI32, not on top of the ESP32 module's metal shield:

| On the shield | Problem |
| --- | --- |
| The shield may be steel (a fridge magnet sticks) | Bends the magnetic field right at the compass |
| The ESP32 warms up, more with WiFi on | Gyro zero drifts and the barometer's height wanders as it warms |
| Overhanging the module's end | Covers the WiFi antenna (the bare PCB trace past the shield) |

Use 3–5 mm **foam** double-sided tape: it damps motor vibration and insulates the GY-91's exposed pads. Keep battery and motor wires as far from it as the frame allows; on a small frame they disturb the compass more than anything else.

## Part 8: Power and motors

### 8.1 Pick the 5V supply for your motors

| Motors | Battery | 5V source for MINI32, receiver, MTF-01 |
| --- | --- | --- |
| Brushless + ESCs | 2S or more | A 5V BEC, or the BEC inside **one** ESC (8.3) |
| Brushed (Tello 8520) | 1S (3.7–4.2V) | A **1S-to-5V boost converter**: 1S is below the \~4.4V the MINI32's 3.3V regulator needs on its 5V pin |

- **One 5V rail, one ground rail.** Run the 5V source to a small 5V bus and ground bus, and connect every device there.
- **Add a 470–1000 µF capacitor** (10V or more) across 5V and GND near the MINI32. Motor spin-ups make the battery sag; if 5V dips below about 4.5V the ESP32 resets, which in the air is a crash.

### 8.2 USB or battery, not both

The MINI32's `5V` pin is tied to the USB 5V line, and no protection diode between them is documented. With both plugged in, two 5V supplies meet with nothing between them, like two water tanks joined by a pipe with no valve, and current can flow back into your PC's USB port.

**Test yours:** USB plugged in **only**, measure `5V` to GND.

| Reads | Meaning |
| --- | --- |
| About 4.6–4.8 V | A diode is in the path: both connected is OK |
| About 5.0 V | Probably straight through: don't connect both |

Until you've tested it: **USB on the bench (battery out), battery in the air.** When you need both (motor test or ESC calibration with Mission Planner open), connect over WiFi instead of USB, or put a Schottky diode (1N5819/SS14) in the 5V supply wire, stripe toward the MINI32.

### 8.3 Brushless ESCs

Each ESC's thin 3-wire plug goes to the MINI32; its thick wires go to the battery and motor.

```
ESC thin plug:
  signal (white/yellow) ──► IO25 / IO27 / IO33 / IO32   (motors 1–4)
  ground (black/brown)  ──► GND                          (never skip it)
  red                   ──► NOT connected, tape the tip  (it's the ESC's 5V OUTPUT)
    exception: if this ESC's BEC is your 5V source, ONE red wire ──► 5V
ESC thick red / black   ──► battery + / −
ESC 3 motor wires       ──► motor, any order (swap any two to reverse it)
```

The ground wire is the shared "sea level": the signal is just 3.3V versus ground, so without a common ground the ESC can't read it. Most ESCs (BLHeli, SimonK) accept the ESP32's 3.3V signal; if one ESC beeps but won't arm while the others work, a 5V-only input is the likely cause.

### 8.4 Brushed motors (Tello 8520) with a MOSFET driver

A brushed motor needs only an on/off switch flicked thousands of times a second; ArduPilot sets how long it stays on (the duty cycle). One logic-level N-MOSFET per motor does it:

```
MINI32 IO25 ──[100 Ω]──┬── MOSFET gate
                       │
                    [10 kΩ] ──► GND      # keeps the motor OFF while the ESP32 boots
MOSFET source ──► GND (battery −)
MOSFET drain  ──► motor −
motor +       ──► battery + (1S)
Schottky diode (1N5819/SS14) across the motor, stripe to battery +   # flyback protection
(repeat for IO27, IO33, IO32)
```

- **Use a logic-level MOSFET** that turns fully on at 2.5–3.3V on its gate, such as AO3400 or SI2302. A standard MOSFET (IRF540 and similar) barely turns on at 3.3V and overheats.
- **Don't skip the 10 kΩ pull-down.** ESP32 pins float during boot; without it a motor can twitch at power-up.
- **Direction is set by polarity.** Tello motors come as CW and CCW pairs; fit them to match the motor layout in Part 7.
- **Set the brushed parameters in 11.1b** (`MOT_PWM_TYPE = 3`, `RC_SPEED`). Skip ESC calibration (12.4).

**Will it lift?** A DJI Tello weighs about 80 g in total, and its motors are sized for that. Your build adds the MINI32, GY-91, FS-A8S, MTF-01, MOSFETs, boost converter, wiring and a frame. **Weigh every part before building the frame**, and measure one motor's thrust with its prop on a kitchen scale: four motors together should give at least **twice** the all-up weight, or ArduPilot has no headroom to stabilise. If the numbers don't work, 1103/1104 brushless motors on 2S with small ESCs are the usual next step.

### 8.5 Battery voltage monitor (optional)

Without it, ArduPilot can't warn you or land when the battery is low. IO35 reads up to about 3.1V, so use a resistor divider sized for your battery:

```
Battery + ──[ R1 ]──┬── IO35
                   │
               [ R2 ]   (plus 100 nF across R2 to steady the reading)
                   │
Battery − ─────────┴── GND
```

| Battery | R1 / R2 | Full battery at IO35 | `BATT_VOLT_MULT` start |
| --- | --- | --- | --- |
| 1S (4.2V) | 10 kΩ / 10 kΩ | 2.1V | 2.0 |
| 3S–4S (12.6–16.8V) | 10 kΩ / 2.2 kΩ | 2.3–3.0V | 5.55 |

Set `BATT_MONITOR = 3`, `BATT_VOLT_PIN = 35`, `BATT_LOW_VOLT = 3.5 × cells`, `BATT_FS_LOW_ACT = 1` (Land). **Calibrate:** measure the battery with a multimeter and set `BATT_VOLT_MULT = old value × (multimeter reading ÷ reported voltage)`.

### 8.6 SD card logging (optional)

The board file uses an SD card in 1-bit mode: CLK on IO14 (`TMS`), CMD on IO15 (`TDO`, 10 kΩ pull-up to 3.3V), D0 on IO2 (10 kΩ pull-up to 3.3V), VCC on 3V3. Use a plain 3.3V microSD breakout (not a 5V "SPI module" with a level shifter), and unplug it while flashing: its pull-up on IO2 can block flash mode. Without a card, keep `LOG_BACKEND_TYPE = 0` (already in defaults.parm, 11.7) so arming doesn't fail with `PreArm: Logging failed`.

## Part 9: Prepare the peripherals

### 9.1 Check the GY-91 chips and its compass

Many cheap GY-91 boards carry an MPU6500 (no compass) or a clone ArduPilot won't recognise. In the Arduino IDE (ESP32 board package, board **ESP32 Dev Module**, which suits the MINI32 too), wire the GY-91 as in Part 7, upload this, and open the Serial Monitor at 115200:

```cpp
#include <SPI.h>
const int CS_MPU = 5, CS_BARO = 26;

uint8_t readReg(int cs, uint8_t reg) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs, LOW);
  SPI.transfer(reg | 0x80);              // 0x80 = read
  uint8_t v = SPI.transfer(0);
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
  return v;
}

void writeReg(int cs, uint8_t reg, uint8_t val) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs, LOW);
  SPI.transfer(reg & 0x7F);              // bit7 = 0 = write
  SPI.transfer(val);
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
}

void setup() {
  Serial.begin(115200);
  pinMode(CS_MPU, OUTPUT);  digitalWrite(CS_MPU, HIGH);
  pinMode(CS_BARO, OUTPUT); digitalWrite(CS_BARO, HIGH);
  SPI.begin(18, 19, 23);                 // SCK, MISO, MOSI
  delay(100);

  // --- let the MPU9250 talk to its internal compass (AK8963 at I2C address 0x0C) ---
  writeReg(CS_MPU, 0x6B, 0x80);          // PWR_MGMT_1: reset MPU
  delay(100);
  writeReg(CS_MPU, 0x6B, 0x01);          // wake up
  writeReg(CS_MPU, 0x6A, 0x30);          // USER_CTRL: I2C master ON + SPI only
  writeReg(CS_MPU, 0x24, 0x0D);          // I2C_MST_CTRL: 400 kHz
  writeReg(CS_MPU, 0x25, 0x0C | 0x80);   // SLV0_ADDR: compass address 0x0C, read
  writeReg(CS_MPU, 0x26, 0x00);          // SLV0_REG: register 0x00 = WIA (compass ID)
  writeReg(CS_MPU, 0x27, 0x81);          // SLV0_CTRL: enable, read 1 byte (repeats automatically)
  delay(10);
}

void loop() {
  uint8_t st = readReg(CS_MPU, 0x36);    // I2C_MST_STATUS: bit0 = 1 -> nobody answered at 0x0C
  Serial.printf("IMU WHO_AM_I: 0x%02X   BARO ID: 0x%02X   MAG @0x0C: %s   MAG ID: 0x%02X\n",
                readReg(CS_MPU, 0x75), readReg(CS_BARO, 0xD0),
                (st & 0x01) ? "NOT FOUND" : "FOUND",
                readReg(CS_MPU, 0x49));  // EXT_SENS_DATA_00: byte fetched from compass
  delay(1000);
}
```

A good board prints:

```
IMU WHO_AM_I: 0x71   BARO ID: 0x58   MAG @0x0C: FOUND   MAG ID: 0x48
```

| Reading | Meaning | Fix |
| --- | --- | --- |
| IMU `0x71` / `0x73` | MPU9250 / MPU9255, with compass | Good |
| IMU `0x70` | MPU6500, **no compass inside** | 11.6 |
| IMU `0x00` or `0xFF` | No answer | Check wiring and power |
| BARO `0x58` | BMP280 | Good |
| MAG `FOUND`, `0x48` | Compass alive | Good. If ArduPilot still shows no compass, check 3.3 and `COMPASS_DEV_ID` |
| MAG `NOT FOUND`, `0x00` (IMU `0x71`) | Compass not answering: its internal bus is stuck, or the compass is dead | Power-cycle, below |

The compass (AK8963) has no pins of its own: it sits inside the MPU9250 on the MPU's internal I2C bus at address `0x0C`, so a normal I2C scanner never finds it. Pressing RST or reflashing doesn't cut power to the GY-91, so a compass interrupted mid-transfer can keep holding its bus:

```
1. Unplug USB for 10 s                  # also the 5V supply, if one is connected
2. Plug in, read the Serial Monitor     # sketch is still on the board
   -> FOUND, 0x48  = bus was stuck, fixed
   -> NOT FOUND    = go to 3
3. Reflash ArduPilot, power-cycle, read COMPASS_DEV_ID
   -> 0 as well    = compass is dead: replace the GY-91 or fit an external I2C compass
```

This sketch replaces ArduPilot, so **reflash** (Part 5) afterwards, then unplug and replug USB once.

### 9.2 FlySky FS-i6 and FS-A8S

- **Bind:** hold the FS-A8S bind button while powering it, then switch on the FS-i6 holding **BIND KEY**. Power-cycle both when the receiver LED goes solid.
- **PPM output:** if IO4 later sees no signal, set **System → RX setup → Output mode → PPM** on the FS-i6.
- **Switches:** **Functions setup → Aux. channels**: Channel 5 = **SwC** (flight modes), Channel 6 = **SwA** (emergency stop).

**Don't skip the channel 5 step.** On the FS-i6, channel 5 starts on the **VrA knob**, not a switch, so flipping SwC does nothing until you change it: hold **OK** → **Functions setup → Aux. channels → Channel 5 → SwC**, then hold **Cancel** to save. SwC is the only 3-position switch, and a 3-position switch lands in ArduPilot's mode slots 1, 4 and 6 (about 1000 / 1500 / 2000 µs), which is why `defaults.parm` sets `FLTMODE1`, `FLTMODE4` and `FLTMODE6`.

**Throttle failsafe (essential).** By default, FlySky receivers keep repeating the last stick values when the link drops. Make the receiver send a throttle value below anything the stick can produce:

1. **Functions setup → End points → Channel 3**: set the low end to **120%**.
2. **Functions setup → Failsafe → Channel 3**: hold the throttle stick fully down and long-press **Down** to store it.
3. **Functions setup → End points → Channel 3**: set the low end back to **100%**.

The failsafe value is now about 900 µs, below ArduPilot's `FS_THR_VALUE` of 975. You'll verify it in Part 12.

### 9.3 MicoAir MTF-01

Configure it **before** connecting it to the MINI32, using the USB-to-TTL adapter and MicoAir's **MicoAssistant**:

| Setting | Value | Why |
| --- | --- | --- |
| Output protocol | `mav_apm` | MAVLink format ArduPilot understands |
| `mav_id` (firmware 4.5+) | `200` | Anything except 1, the flight controller's own ID |
| Baud | 115200 | Matches `SERIAL3_BAUD = 115` |

Mount it pointing straight down, arrow forward, with a clear view of the floor. Optical flow needs a textured floor and decent light; plain white or glossy floors give poor readings.

# Section 3: ArduPilot parameter settings

By the end of this section Mission Planner is connected, every parameter is set, the sensors and radio are calibrated, and the frame has passed a props-off bench test.

## Part 10: Connect Mission Planner

### 10.1 Over USB (best for setup)

The MINI32's USB port goes through its USB chip to ArduPilot's SERIAL0.

1. If you flashed from WSL, give the board back to Windows first (Part 5, Option B, step 3).
2. Set the reset option once (Part 6.1): **Config → Planner**, tick only **Reset on USB Connect (toggle DTR)**.
3. In Mission Planner (top right), choose the board's **COM port** and **115200**, then click **Connect**.

**Check:** the parameter download starts within a few seconds and the board doesn't reboot (its WiFi network doesn't disappear) when you connect.

### 10.2 Over WiFi (good for monitoring)

Join the network named in your board file (`WIFI_SSID`, password `WIFI_PWD`). In Mission Planner choose **TCP**, host **192.168.4.1**, port **5760**. That's the ESP32's default access-point address, listed in ArduPilot's ESP32 README; `ipconfig` in PowerShell shows it as the **Default Gateway**.

If WiFi drops or parameters never finish loading, check in order: is the board restarting (network vanishes briefly: catch it with 6.3)? Is Windows hopping back to your home WiFi (untick **Connect automatically** on it)? Is the antenna end near metal or wires? Is the 5V supply weak? Whatever the cause, use **USB for parameter loading and calibration** and WiFi only for monitoring.

### 10.3 First-connection checks

- **Messages tab** (Data → Messages): the IMU, barometer and compass are detected. Some `PreArm` messages are normal until calibration is done.
- **HUD:** tilt the board and the horizon follows smoothly.
- **Full Parameter List** (Config) loads completely.

**What the first Messages mean** (newest at the top, so read upwards): `EKF3 IMU0 forced reset` is normal at start-up; `initialised` means the estimator runs; `tilt alignment complete` means the IMU knows which way is down; `MAG0 initial yaw alignment complete` means the compass is being used.

**HUD shows a ground speed (GS) and a wandering horizon while the board sits still?** The EKF has no velocity source yet. With the MTF-01 not connected, use the bench settings in 11.4b.

## Part 11: Set the ArduPilot parameters

In **Config → Full Parameter List**, change one group at a time, click **Write Params**, then **reboot** (press RST). Several settings only take effect after a reboot. Use USB.

> **If you built with `defaults.parm` (3.4) and erased the flash, every value in 11.0 and 11.2–11.5 is already set.** Only MAV3\_OPTIONS (11.4) needs setting by hand, plus the 11.4b bench values while the MTF-01 isn't wired. Use the tables to check the rest and to understand what each does. Brushed-motor builds still need 11.1b.

If Mission Planner shows **Failed** when writing a value, it usually means its confirmation got lost on the slow link, not that the board refused. Press **Refresh Params**: if the new value is shown, the write worked. A real refusal shows `Param write denied` in the Messages tab.

### 11.0 Loop rate (set this first)

ArduCopter runs its control loop 400 times a second by default and, since Copter 4.3.2, refuses to arm if the board can't keep up (`PreArm: Main loop slow`). The ESP32 can't. Like a juggler told to catch a ball every 2.5 ms who can only manage every 4.4 ms, it needs an honest target.

| Parameter | Value | Meaning |
| --- | --- | --- |
| `SCHED_LOOP_RATE` | 150 | Control loop 150 times a second, a rate the ESP32 holds (ArduPilot's own ESP32-S3 StampFly board uses 150) |

If the PreArm message still shows a lower measured rate, choose a round number below it. Expect softer control than a 400 Hz flight controller; start with lower gains, and don't bypass the check by disabling `ARMING_CHECK`.

### 11.1a Frame and motors: brushless ESCs

| Parameter | Value | Meaning |
| --- | --- | --- |
| `FRAME_CLASS` | 1 | Quad |
| `FRAME_TYPE` | 1 | X layout |
| `MOT_PWM_TYPE` | 0 | Normal PWM (no DShot on this port) |
| `MOT_PWM_MIN` / `MOT_PWM_MAX` | 1000 / 2000 | PWM range sent to the ESCs |
| `SERVO1_FUNCTION` … `SERVO4_FUNCTION` | 33, 34, 35, 36 | Motors 1–4 on IO25, 27, 33, 32 |
| `AHRS_ORIENTATION` | 0 | GY-91 arrow forward; change only if mounted differently |

### 11.1b Frame and motors: brushed (Tello 8520 + MOSFETs)

Same as 11.1a, except (from ArduPilot's [Brushed Motors](https://ardupilot.org/copter/docs/common-brushed-motors.html) page):

| Parameter | Value | Meaning |
| --- | --- | --- |
| `MOT_PWM_TYPE` | 3 | Brushed: throttle sent as a duty cycle. Reboot after setting |
| `RC_SPEED` | 16000 | PWM frequency 16 kHz (1000–20000 allowed). Mission Planner warns because it's above the usual 490; accept it |

The ESP32 port has a brushed output mode, but its behaviour at 16 kHz hasn't been confirmed. On the bench, the motors should run smoothly through the whole throttle range; if they stutter or step, try `RC_SPEED = 8000`.

### 11.2 Radio and flight modes

| Parameter | Value | Meaning |
| --- | --- | --- |
| `RCMAP_ROLL` / `PITCH` / `THROTTLE` / `YAW` | 1 / 2 / 3 / 4 | Matches the FS-i6's channel order (the defaults) |
| `FLTMODE_CH` | 5 | Flight modes from channel 5 (SwC) |
| `FLTMODE1` | 0 | SwC up: **Stabilize** |
| `FLTMODE4` | 2 | SwC middle: **AltHold** |
| `FLTMODE6` | 5 | SwC down: **Loiter** (needs optical flow) |
| `FLTMODE2`, `3`, `5` | 0, 0, 2 | Safe fillers for in-between switch values |
| `RC6_OPTION` | 31 | SwA = **motor emergency stop** |

### 11.3 Failsafes

| Parameter | Value | Meaning |
| --- | --- | --- |
| `FS_THR_ENABLE` | 3 | On radio loss, always **Land** |
| `FS_THR_VALUE` | 975 | Throttle below this counts as radio loss (matches the 9.2 receiver failsafe) |
| `FS_GCS_ENABLE` | 0 | Don't failsafe when the WiFi ground-station link drops |
| `FS_EKF_ACTION` | 1 | If the position estimate fails, Land |

### 11.4 MTF-01 on SERIAL3

| Parameter | Value | Meaning |
| --- | --- | --- |
| `SERIAL3_PROTOCOL` | 1 | MAVLink on IO16/17. SERIAL3 defaults to GPS, so this **must** change |
| `SERIAL3_BAUD` | 115 | 115200 baud |
| `SERIAL3_OPTIONS` | 0 | Leave at 0: bit 10 (1024) blocks arming on 4.8 firmware |
| `FLOW_TYPE` | 5 | Optical flow from MAVLink |
| `RNGFND1_TYPE` | 10 | Rangefinder from MAVLink |
| `RNGFND1_MAX` / `RNGFND1_MIN` | 8 / 0.01 | Range in metres (set after a reboot) |
| `RNGFND1_ORIENT` | 25 | Pointing down |
| `FLOW_ORIENT_YAW` | 0 | MTF-01 arrow forward |
| `MAV3_OPTIONS` | 2 | Don't forward MAVLink traffic to the sensor (the 4.7+ replacement for SERIAL3\_OPTIONS 1024). MAV numbers count MAVLink ports in order: USB = MAV1, WiFi = MAV2, MTF-01 = MAV3. Appears after a reboot |

**Why not `SERIAL3_OPTIONS = 1024`?** Older MTF-01 guides use it. In ArduPilot 4.7 that switch moved to `MAVn_OPTIONS` (value 2), and 4.7–4.9 refuse to arm with `SERIAL3_OPTIONS bit 10 ('no forward mavlink') must not be set; use MAVn_OPTIONS bit 1 instead`. Leave `SERIAL3_OPTIONS` at 0.

**Can't find `FLOW_TYPE`?** Search **Config → Full Parameter List** for `FLOW` over USB after **Refresh Params**. If it's still missing while `RNGFND1_TYPE` is there, flow isn't in the firmware: run 3.2 again, rebuild starting with configure, confirm with the Part 4 check, then reflash. No erase is needed: FLOW\_TYPE was never saved, so it takes its value from defaults.parm.

### 11.4b Bench-testing before the MTF-01 is connected

With the sensor missing, ArduPilot waits for data that can't arrive: `PreArm: Rangefinder 1: No Data`, a fake ground speed, and a wandering horizon. Switch flow off and move height to the barometer until it's wired, then **Write Params** and reboot:

| Parameter | Bench value | Restore when MTF-01 is wired |
| --- | --- | --- |
| `EK3_SRC1_VELXY` | 0 | 5 |
| `RNGFND1_TYPE` | 0 | 10 |
| `FLOW_TYPE` | 0 | 5 |
| `EK3_SRC1_POSZ` | 1 (barometer) | 2 (rangefinder, indoor) |

**Check:** the rangefinder PreArm is gone and GS reads about 0. Stabilize and AltHold work like this; Loiter waits for flow.

### 11.5 Position estimate (EKF3) for indoor flight

| Parameter | Value | Meaning |
| --- | --- | --- |
| `EK3_SRC1_POSXY` | 0 | No absolute position (no GPS) |
| `EK3_SRC1_VELXY` | 5 | Horizontal velocity from optical flow |
| `EK3_SRC1_POSZ` | 2 | Height from the MTF-01 rangefinder (indoor setup; 1 = barometer) |
| `EK3_SRC1_VELZ` | 0 | No vertical velocity source |
| `EK3_SRC1_YAW` | 1 | Heading from the compass |

`AHRS_EKF_TYPE = 3` (EKF3) and `EK3_SRC_OPTIONS = 0` complete MicoAir's indoor list; both are already the defaults in current firmware.

**Why rangefinder height indoors.** ArduPilot's general optical-flow page uses the barometer (`POSZ = 1`), written for any vehicle including outdoor ones. MicoAir's MTF-01 guide uses the rangefinder (`POSZ = 2`) for indoor-only flight. Indoors the barometer drifts with heat (the GY-91 sits near a warm ESP32), air conditioning and the props' own downwash, while the laser reads height to the centimetre, and an indoor ceiling keeps you well inside its 8 m range.

|  | Barometer (`1`) | Rangefinder (`2`, used here) |
| --- | --- | --- |
| Height accuracy indoors | Drifts | Centimetre-level, steady |
| Over a table or box | Holds height | Measures a “higher floor” and **climbs** |
| Shiny or very dark floor, sun on the floor | Unaffected | Can lose the reading |
| MTF-01 stops sending | Height still known | No height: EKF failsafe lands (`FS_EKF_ACTION = 1`) |

**Rules for rangefinder height:**

1. Fly over a plain, flat, matt floor with some texture (a patterned rug or tape marks help optical flow too).
2. Before flying, hold the frame 30–50 cm up and watch `rangefinder1` in **Data → Status**: steady, no jumps to 0.
3. Do the AltHold stage tethered (13.2) before trying Loiter.
4. **Without the MTF-01 connected, `POSZ = 2` leaves the EKF with no height source at all**, so always use the 11.4b bench settings until it's wired.

If you later fly outdoors or over uneven ground, switch to `EK3_SRC1_POSZ = 1` with `EK3_RNG_USE_HGT = 70`: the barometer becomes primary and the rangefinder is used below 70% of `RNGFND1_MAX` (and under 2 m/s, `EK3_RNG_USE_SPD`), with the barometer as backup.

After the first hovers, follow ArduPilot's **Optical Flow** setup page to calibrate `FLOW_FXSCALER` / `FLOW_FYSCALER`.

### 11.6 Compass

- **Compass found** (`COMPASS_DEV_ID` not 0): keep `COMPASS_ENABLE = 1` and calibrate it (12.2).
- **MPU6500, or compass dead after the 9.1 power-cycle test:** set `COMPASS_ENABLE = 0`, fly only Stabilize and AltHold, and read ArduPilot's non-GPS, compass-less guidance before using Loiter.

Indoors, steel and power cables distort any compass, and on a small frame the motor wires sit close to the GY-91. Keep power wires as far from it as the frame allows.

### 11.7 Logging

The MINI32 has no SD card, so `defaults.parm` sets `LOG_BACKEND_TYPE = 0` (bit 0 = SD card file, bit 1 = MAVLink, bit 2 = block flash; 0 = off). That avoids `PreArm: Logging failed` and frees the 16 KB log buffer. The `sdmmc_card_init failed` lines at boot stay; they're harmless. Set it to `1` (and reboot) once you fit an SD card (8.6): flight logs are the main tool for diagnosing vibration and crashes.

## Part 12: Calibrate

In **Setup → Mandatory Hardware**, over USB, **props off**. Reboot after each.

| Step | How | Check |
| --- | --- | --- |
| 12.1 Accelerometer | **Calibrate Accel** (six positions), then **Calibrate Level** with the frame level | HUD level when the frame is level |
| 12.2 Compass | **Start**, rotate the frame slowly through every orientation, outdoors, away from steel and cables | HUD heading changes correctly as you turn |
| 12.3 Radio | **Calibrate Radio**: sticks to every corner, every switch through every position, **Click when Done** | Right stick = roll/pitch (stick up shows pitch bar down, which is correct), left stick = throttle/yaw, SwC = ch5 and mode changes, SwA = ch6 |
| 12.4 ESCs (brushless only) | `ESC_CALIBRATION = 3`, full steps in 12.6 | Resets itself; all motors start together. BLHeli\_S ESCs skip this |
| 12.5 Flow sensor | Hold 30–50 cm over a textured floor, **Data → Status** | `rangefinder1` tracks height, `opt_qua` well above 0, `opt_m_x`/`opt_m_y` change as you move |

**Test the radio failsafe:** with the receiver powered, switch the transmitter **off**. **Check:** channel 3 drops to about 900 and Mission Planner reports a radio failsafe. If it stays at its last value, redo 9.2.

### 12.6 ESC calibration in detail (brushless only)

ArduPilot runs the calibration itself, but its code (`ArduCopter/esc_calibration.cpp`) checks the radio first: it waits **at most 2 seconds** after boot for the receiver, and if the radio checks fail it **silently skips** calibration and resets `ESC_CALIBRATION` to 0. So the receiver must already be linked when the board powers up.

**Before you start:**

- [ ] Props off
- [ ] Radio calibrated (12.3) and the FS-A8S LED solid with the FS-i6 on
- [ ] MINI32 powered from the battery's 5V (BEC), **USB unplugged**, so board and ESCs start together
- [ ] `MOT_PWM_TYPE = 0`, `MOT_PWM_MIN = 1000`, `MOT_PWM_MAX = 2000`

**Method A, semi-automatic (easiest; the MINI32 has no LEDs or buzzer to watch):**

```
1. Mission Planner (USB): ESC_CALIBRATION = 3 -> Write Params
2. Unplug USB and battery                   # board fully off
3. FS-i6 ON, throttle stick DOWN
4. Plug in the battery                      # board sends max throttle ~5 s, then min
5. Listen: tone + 2 beeps (max learned) -> beeps per cell -> long beep (min learned)
6. Unplug the battery                       # ESC_CALIBRATION resets itself to 0
```

**Method B, throttle stick:**

```
1. FS-i6 ON, throttle stick FULL UP          # must read 95% or more after radio calibration
2. Plug in the battery                       # board notes high throttle, then waits
3. Unplug the battery, stick still UP
4. Plug it in again                          # your stick now goes straight to the ESCs
5. Tone, 1 beep per cell, 2 beeps            # max captured
6. Stick FULL DOWN -> long tone              # min captured
7. Nudge throttle: all 4 start together -> stick down -> unplug battery
```

**Check:** power up normally and run **Motor Test** at 5–10%: all four start at the same percentage. Brushed motors skip this (ArduPilot does too). A DIY ESC only calibrates if its firmware learns its range at power-up; otherwise set `MOT_PWM_MIN`/`MAX` to the range it expects. Source: [ArduPilot ESC Calibration](https://ardupilot.org/copter/docs/esc-calibration.html).

### 12.7 Flight-mode switch check

Flip **SwC** up, middle, down and watch the HUD mode and **Setup → Flight Modes** (the highlighted row follows the switch):

| What you see | Cause | Fix |
| --- | --- | --- |
| Channel 5 bar doesn't move in Radio Calibration | FS-i6 channel 5 still on the VrA knob | 9.2: Aux. channels → Channel 5 → SwC |
| Bar moves between only 2 positions | A 2-position switch | Use SwC |
| Bar moves, mode row doesn't | `FLTMODE_CH` not 5 (an old saved value wins over `defaults.parm`) | Set `FLTMODE_CH = 5` |
| Row moves, HUD says `Flight mode change failed` | ArduPilot refusing the mode | Normal for Loiter until flow works; AltHold needs a working height source (rangefinder, or the barometer with the 11.4b bench settings) |

## Part 13: Bench test, then first flight

### 13.1 Bench test (props OFF)

Work through these in order; each line rules out a cause before the next depends on it.

- [ ] Boot shows the IMU, barometer and compass detected, with no `INS: unable to initialise driver`
- [ ] HUD follows tilt smoothly, with no drift while still
- [ ] Altitude changes when you lift the frame (barometer with the 11.4b bench settings; rangefinder once the MTF-01 is wired)
- [ ] Radio bars correct; SwC switches Stabilize / AltHold / Loiter
- [ ] Transmitter off triggers radio failsafe
- [ ] Flow and rangefinder values respond (12.5)
- [ ] **Motor Test** (Setup → Optional Hardware): test A = front right, B = rear right, C = rear left, D = front left; each spins the right way (brushless: swap two motor wires; brushed: swap the motor's two wires)
- [ ] Arm in Stabilize (throttle down, rudder right \~2 s): motors idle
- [ ] Tilt the frame by hand: the low-side motors speed up
- [ ] SwA emergency stop cuts the motors instantly
- [ ] Disarm (throttle down, rudder left \~2 s): motors stop

If arming is refused, read the `PreArm:` message and fix that cause. Don't disable `ARMING_CHECK`. **Props on** only when every line passes: front-right and rear-left take counter-clockwise props, front-left and rear-right clockwise.

### 13.2 First flight, in stages

Tether the frame or fly inside a net, and keep everyone behind you.

| Stage | Mode | Goal | Move on when |
| --- | --- | --- | --- |
| 1 | Stabilize | Throttle up until light on its feet, then land | No flip, no strong drift |
| 2 | Stabilize | Hover at 30–50 cm for 10–20 s | Holds attitude with small corrections |
| 3 | AltHold | Switch from a Stabilize hover | Holds height within about 20 cm |
| 4 | Loiter | Over a textured floor | Holds position hands-off |

**Before every flight:** battery charged, props tight and on the right motors; HUD level, no PreArm errors, mode switch on Stabilize; transmitter on **before** the battery, battery out **before** the transmitter off; finger on SwA.

| What you see | Likely cause |
| --- | --- |
| Flips immediately on take-off | Wrong motor order, spin direction, or prop placement |
| Needs near-full throttle to lift | Too heavy for the motors (8.4 thrust check) |
| Drifts steadily one way in Stabilize | Level not calibrated, or centre of gravity off |
| Fast wobble or oscillation | Gains too high, or vibration reaching the GY-91 |
| Climbs or sinks in AltHold | Floor height changing under it, a weak laser reading (shiny or dark floor), or vibration; with bench settings, prop wash on the barometer (cover it with open-cell foam) |
| Wanders in Loiter | Poor floor texture or light, uncalibrated flow, bad compass |

# Appendix

## Part 14: Troubleshooting

Find your message, then apply the fix. When asking for help, include the **first** error line or the exact `PreArm` message, your Ubuntu version, your USB chip, and which Part you were on.

### Build environment

| Error | Cause | Fix |
| --- | --- | --- |
| `Aborting` / `Unable to checkout` in submodules | Stray files from an interrupted run | Submodule repair, 2.3 |
| `Python being checked: ...idf4.2_py3.8_env...` | Old `IDF_PYTHON_ENV_PATH` set | `grep -rn IDF_PYTHON_ENV_PATH ~/.bashrc ~/.profile ~/.bash_profile`, remove it, new terminal |
| `can not create a virtual environment again` | Terminal is inside another environment | New terminal; `echo $VIRTUAL_ENV` must be empty |
| `ensurepip is not available` | venv package missing | Install `python3-venv`, delete the half-made environment, rerun `install.sh` |
| `Python minimum supported version is 3.9.0` | Ubuntu 20.04 (Python 3.8) | Use Ubuntu 22.04 or newer (Part 2) |
| `Cannot import module "esp_idf_monitor"` | Wrong Python first in PATH | New terminal, then `get_idf` once |
| `ninja: error: loading 'build.ninja'` | Skipped plain configure | `./waf configure`, then with `--board` |
| `Missing configuration file .../hwdef.h` | Configures run out of order | `./waf configure --board=esp32lukas` again, then build |
| `No module named em` | ArduPilot modules not in the IDF environment | `python3 -m pip install empy==3.3.4 pexpect` after `get_idf` |
| `Error: Duplicate MAG` | A compass line is in `hwdef.dat` twice | Recreate the file with 3.1 |

### Flashing and USB

| Symptom | Cause | Fix |
| --- | --- | --- |
| No COM port in Windows | USB driver missing | Install the driver for your chip (5.1) |
| No `/dev/ttyUSB0` in WSL | Board not attached, or it's a CH9102 | `usbipd attach --wsl --busid <id>`; look for `/dev/ttyACM0` |
| `Failed to connect to ESP32` | Not in flash mode, or charge-only cable | IO0-to-GND jumper method (Part 5); another cable |
| `Unable to verify flash chip connection (Guru Meditation...)` | Running firmware is crashing | Jumper method with `--before no_reset` |
| `Permission denied` on the port | Not in `dialout` group | `sudo usermod -aG dialout $USER`, log out and in |
| Board missing in Windows | Still attached to WSL | `usbipd detach` then `usbipd unbind` |
| Boot loop | Bad flash, weak power, GPIO12 pulled high, or IO0 jumper left in | Remove jumper; erase and reflash; good 5V; free GPIO12 (`TDI`) |
| Won't flash with SD module fitted | Pull-up on GPIO2 | Unplug the SD module while flashing |

### Running, connection and WiFi

| Symptom | Cause | Fix |
| --- | --- | --- |
| Very slow connect, or board reboots on connect | Mission Planner toggling DTR/RTS | **Config → Planner**: tick only DTR reset; untick both if still slow (6.1) |
| `erasing EEPROM` on every boot | Board restarts before saving | Treat as a crash; 6.3 |
| WiFi drops, network vanishes briefly | Board restarting | 6.3 to catch the reason |
| Parameters never finish over WiFi | Slow or dropping link | Use USB for setup (10.1) |
| **Failed** when writing a parameter | Lost confirmation on a slow link | **Refresh Params** to check; write over USB, one at a time |
| Resets when throttle goes up | 5V brownout | Capacitor and a stronger 5V source (8.1) |
| `No MAVLink Heartbeat Packets` | Port held by another program, AUTO baud, reset on connect, or board restarting | 6.1b checklist |
| Board reboots by itself | Read the rst: code at the top of the boot log | Part 5 reset-reason table |

### Sensors, radio, motors and arming

| Symptom | Cause | Fix |
| --- | --- | --- |
| `INS: unable to initialise driver` | IMU wiring, power or clone chip | Part 7 wiring by GPIO label; chip check 9.1 |
| `PreArm: Compass 1 not healthy` | Compass lines missing from the board file | 3.1, rebuild, flash |
| No compass (`COMPASS_DEV_ID = 0`) | MPU6500, or compass bus stuck or dead | 9.1 power-cycle test; 11.6 |
| No barometer | CSB wiring | CSB to IO26; chip ID `0x58` |
| No RC input | iBUS pad, wrong pin, unbound or unpowered receiver | PPM pad to IO4, 5V power, rebind, PPM output on |
| Failsafe doesn't trigger | Receiver failsafe not set | Redo 9.2, re-test 12 |
| No flow or rangefinder data | Port setting, crossover, MTF-01 setup | `SERIAL3_PROTOCOL = 1`, TX→RX crossover, `mav_apm`, `mav_id = 200` |
| `PreArm: Main loop slow` | Loop rate above what the ESP32 can hold | `SCHED_LOOP_RATE = 150` or lower (11.0) |
| `PreArm: SERIAL3_OPTIONS bit 10 ... must not be set` | Old option value | `SERIAL3_OPTIONS = 0` |
| `PreArm: Logging failed` | No SD card | `LOG_BACKEND_TYPE = 0` (11.7), or fit a card (8.6) |
| Motor twitches at power-up (brushed) | Missing gate pull-down | 10 kΩ gate-to-GND (8.4) |
| MOSFET hot, motor weak (brushed) | Not a logic-level MOSFET | AO3400 / SI2302 (8.4) |
| One motor doesn't spin (brushless) | Signal wire, missing signal ground, ESC not calibrated | Check wiring; 12 |
| `FLOW_TYPE` not in the parameter list | ESP32 port switches optical flow off | Run 3.2, rebuild starting with configure, Part 4 check, reflash |
| `PreArm: Rangefinder 1: No Data` | MTF-01 not connected, or no data on SERIAL3 | Bench settings 11.4b; or wiring and MicoAssistant setup 9.3 |
| Ground speed shown and horizon wanders while still | EKF set to use flow, but no flow data; or accel not calibrated | 11.4b, then 12.1 |
| Mode switch does nothing | FS-i6 channel 5 on the VrA knob | 9.2 and 12.7 |
| ESC calibration does nothing | Radio not linked within 2 s of power-up, or radio not calibrated | Transmitter on first; 12.3; then 12.6 |
| Parameter write shows Failed | Confirmation lost on a slow link | Refresh Params to check; write over USB |

## Quick reference

**Everyday build and flash** (new Ubuntu terminal):

```bash
cd ~/ardupilot_esp32_build/ardupilot

get_idf                                                   # load the tools

./waf configure --board=esp32lukas                        # after building another board or editing hwdef.dat

ESPPORT=/dev/ttyUSB0 ESPBAUD=921600 ./waf copter --upload  # build and flash (ttyACM0 for CH9102)
```

**Before flashing a board-file change**, confirm flow is in: `nm -C build/esp32lukas/esp-idf_build/ardupilot.elf | grep -c AP_OpticalFlow_MAV` must print more than 0.

**Updating ArduPilot** (keeps the 3.2 change):

```bash
cd ~/ardupilot_esp32_build/ardupilot

git stash                                   # set aside the esp32.h change

git pull && git submodule update --init --recursive

git stash pop                               # put it back

# then run the 3.2 block again, configure, build, and the Part 4 check
```

**WSL USB cycle** (PowerShell):

```powershell
usbipd bind --busid 1-1                # Admin; once per board, or after unbind

usbipd attach --wsl --busid 1-1        # lend to Linux for flashing

usbipd detach --busid 1-1              # give back to Windows

usbipd unbind --busid 1-1              # Admin; stop sharing with WSL
```

**Pins**

| Function | MINI32 GPIO |
| --- | --- |
| SPI clock / MOSI / MISO | 18 / 23 / 19 |
| MPU9250 CS / BMP280 CS | 5 / 26 |
| RC PPM input | 4 |
| Motors 1–4 (5–6 spare) | 25, 27, 33, 32 (22, 21) |
| MTF-01, SERIAL3 RX / TX | 16 / 17 |
| Battery voltage (optional) | 35 |
| SD card CLK / CMD / D0 (optional) | 14 (`TMS`) / 15 (`TDO`) / 2 |
| Manual flash mode | IO0 to GND, press RST |
| Leave free | 0, 6–11, 12 (`TDI`), 13 (`TCK`) |

**Ports**

| ArduPilot port | Link |
| --- | --- |
| SERIAL0 | USB cable, 115200 (Mission Planner) |
| SERIAL1 | WiFi, TCP 192.168.4.1 port 5760 |
| SERIAL2 | Not defined on this board |
| SERIAL3 | IO16/17, MTF-01 |

**Key parameters**

| Group | Settings |
| --- | --- |
| Loop | `SCHED_LOOP_RATE 150` (set first) |
| Frame | `FRAME_CLASS 1`, `FRAME_TYPE 1`, `MOT_PWM_TYPE 0` (brushed: `3` + `RC_SPEED 16000`) |
| Modes | `FLTMODE_CH 5`, `FLTMODE1 0`, `FLTMODE4 2`, `FLTMODE6 5`, `RC6_OPTION 31` |
| Failsafe | `FS_THR_ENABLE 3`, `FS_THR_VALUE 975`, `FS_GCS_ENABLE 0`, `FS_EKF_ACTION 1` |
| Flow | `SERIAL3_PROTOCOL 1`, `SERIAL3_BAUD 115`, `SERIAL3_OPTIONS 0`, `FLOW_TYPE 5`, `RNGFND1_TYPE 10`, `RNGFND1_MAX 8`, `RNGFND1_MIN 0.01` |
| EKF | `EK3_SRC1_POSXY 0`, `EK3_SRC1_VELXY 5`, `EK3_SRC1_POSZ 2`, `EK3_SRC1_VELZ 0`, `EK3_SRC1_YAW 1` |
| Flow port | `MAV3_OPTIONS 2` (never SERIAL3\_OPTIONS 1024) |
| Logging | `LOG_BACKEND_TYPE 0` without SD card |
| Bench, no MTF-01 | `EK3_SRC1_POSZ 1, EK3_SRC1_VELXY 0, RNGFND1_TYPE 0, FLOW_TYPE 0` |

**Order of work:** check board → environment → board file + flow fix → build (check flow is inside) → flash bare board → stability test → wire one device at a time → peripherals → Mission Planner over USB → parameters → calibration → bench test → props on → tethered hover.

## Sources

- [ESP32 Based Autopilot – ArduPilot Dev docs](https://ardupilot.org/dev/docs/esp32-autopilot.html)
- [Brushed Motors – ArduPilot Copter docs](https://ardupilot.org/copter/docs/common-brushed-motors.html)
- [Wemos D1 Mini ESP32 pinout and specs – espboards.dev](https://www.espboards.dev/esp32/d1-mini32/)
- [MH-ET LIVE MiniKit board docs – RIOT OS](https://doc.riot-os.org/boards/esp32-mh-et-live-minikit)
- [ESP32 incorrect VIN power – espboards.dev](https://www.espboards.dev/troubleshooting/issues/power/esp32-incorrect-vin/)
- [8520 brushed coreless motors for DJI Tello – Maison du Drone](https://www.maisondudrone.com/en/produit/dji-en/dji-tello-en/2-motors-cwccw-2-1v-8520-8-5x20mm-28000rpm-brushed-coreless-for-dji-tello/)
- ArduPilot source (master, October 2026): `libraries/AP_HAL_ESP32/hwdef/esp32buzz/hwdef.dat`, `esp32nick/hwdef.dat`, `AP_HAL_ESP32/RCOutput.cpp`
- [MicoAir MTF-01 Optical Flow Sensor – ArduPilot Copter docs](https://ardupilot.org/copter/docs/common-mtf-01.html)
- [Optical Flow Sensor Setup – ArduPilot Copter docs](https://ardupilot.org/copter/docs/common-optical-flow-sensor-setup.html)
- [ESC Calibration – ArduPilot Copter docs](https://ardupilot.org/copter/docs/esc-calibration.html)
- MicoAir MTF-01 user guide, Step 5 (indoor EKF3 settings)
- ArduPilot source (master, October 2026): `libraries/AP_HAL/board/esp32.h` (optical flow off), `ArduCopter/esc_calibration.cpp`, `AP_SerialManager.cpp` (SERIAL OPTIONS bit 10 check)
