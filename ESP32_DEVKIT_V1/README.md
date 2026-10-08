# ArduPilot on ESP32 DevKit — Comprehensive User Guide

Oct 7, 2026 · @lukas

This guide takes you from a bare Windows PC to a bench-tested quadcopter running ArduCopter on a **classic ESP32 DevKit**, with a GY-91 IMU, a FlySky FS-A8S receiver and a MicoAir MTF-01 optical-flow sensor. It brings together everything learned so far: the Ubuntu 20.04 workarounds that were proven on a real install, the three ways to flash, the usbipd attach/detach cycle, and how to test the bare board for crashes before you wire anything.

> **Status and safety.** ArduPilot on ESP32 is experimental. No commercial ESP32 autopilot exists, only 4 PWM outputs are confirmed working, and there is no DShot. Keep props **off** until Part 13, fly tethered first, and never near people. Pin numbers, serial ports and flash layout here were checked against ArduPilot master's `esp32buzz` board definition (October 2026).

**Why the classic DevKit and not the ESP32-S3 Super Mini.** The Super Mini needed a custom board file, a flash-size change and WiFi-only setup, and its firmware crashed repeatedly (`Guru Meditation Error (IllegalInstruction)`). The classic DevKit uses ArduPilot's own `esp32buzz` board, which was built around a GY-91, and its USB port gives a reliable wired link to Mission Planner. Get this working first; it's the baseline everything else is compared against.

## How to use this guide

Work through the Parts **in order**. Each step ends with a **Check**; don't move on until it passes. Most problems in this project came from carrying on after a step had silently failed.

Command blocks are written to be copied as they are. The text after `#` on each line explains what that line does; the shell ignores it.

**The guide has three sections, plus an appendix:**

| Section | Covers | Parts |
| --- | --- | --- |
| **1. Install, build and flash** | Choosing the board, setting up Ubuntu, building ArduPilot, flashing it, and checking the bare board is stable | 1–6 |
| **2. Schematic and wiring** | The wiring diagram, pin tables, optional extras, and preparing the GY-91, receiver and MTF-01 | 7–9 |
| **3. ArduPilot parameter settings** | Connecting Mission Planner, every parameter to set, calibration, bench test and first flight | 10–13 |
| Appendix | Troubleshooting and a one-page quick reference | 14, Quick reference |

Do the sections in order: each one assumes the previous one passed its checks.

# Section 1: Install, build and flash

By the end of this section ArduPilot is built on your Linux machine, flashed onto the bare ESP32, and proven not to crash or reset.

## Part 1: Choose and check the board

### 1.1 Parts list

| Part | Role | Connects over |
| --- | --- | --- |
| ESP32 DevKit V1 (classic ESP32, WROOM module) | Flight controller | — |
| GY-91 (MPU9250 + BMP280) | IMU and barometer | SPI |
| FlySky FS-A8S + FS-i6 transmitter | Pilot input | PPM (one wire) |
| MicoAir MTF-01 | Optical flow + downward rangefinder | UART (MAVLink) |
| 4 ESCs + motors, quad X frame | Propulsion | PWM |
| 5V BEC | Clean 5V supply | — |
| 3.3V USB-to-TTL adapter (CP2102/CH340/FTDI) | Configuring the MTF-01 | — |

### 1.2 What ArduPilot expects from the board

| Item | Requirement | Why |
| --- | --- | --- |
| Chip | Classic ESP32 | ArduPilot's `esp32buzz` board targets it |
| Module | **WROOM**-32, -32D, -32E or -32UE | **Not WROVER**: WROVER uses GPIO16/17 for its PSRAM, and those are the MTF-01's pins |
| Flash | 4 MB or more | Firmware (3 MB) plus parameter storage (256 KB) need about 3.3 MB |
| USB chip | CP2102 or CH340 | Gives Mission Planner a wired link through the real serial port |
| Pins broken out | 1, 3, 4, 5, 16–19, 23, 25–27, 32, 33 | Standard 30- and 38-pin DevKits have all of them |

Avoid ESP32-C3, -C6 and -S2 boards: ArduPilot doesn't support them.

### 1.3 Check your board

After Part 2 is set up, plug the board in and run:

```bash
get_idf                                                  # load the ESP32 tools
esptool.py --chip esp32 -p /dev/ttyUSB0 flash_id         # ask the chip about itself
```

**Check:** the output includes `Chip is ESP32-D0WD...` (classic ESP32) and `Detected flash size: 4MB` (or more). The module name is printed on its metal shield: it should say WROOM, not WROVER.

### 1.4 How ArduPilot uses the 4 MB flash

| Address | Size | Contents |
| --- | --- | --- |
| 0x1000 | \~28 KB | Bootloader |
| 0x8000 | 4 KB | Partition table |
| 0x9000 | 24 KB | NVS (ESP-IDF settings, including WiFi) |
| 0xF000 | 4 KB | Radio calibration |
| 0x10000 | 3 MB | ArduCopter firmware |
| 0x310000 | 256 KB | ArduPilot storage: **your parameters and calibration** |

Normal re-flashing leaves the storage area alone, so calibration survives firmware updates. `erase_flash` wipes everything, including storage, which is why you only use it before the very first flash.

## Part 2: Set up the build environment

The build runs in Ubuntu, either natively or in WSL2 on Windows. Check your version with `lsb_release -a`:

|  | Path A | Path B |
| --- | --- | --- |
| Ubuntu | 22.04 or newer | 20.04 |
| Python | 3.10+, works as is | 3.8, too old for ArduPilot; needs a 3.9 shim |
| Status | Expected to work | **Tested and working** (6 Oct 2026) |

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
sudo apt install -y python3.9 python3.9-venv python3.9-dev python3.8-venv   # Path B only
```

**Check:** `python3 -m venv --help > /dev/null && echo "venv OK"` prints `venv OK`.

### 2.2 Python 3.9 shim (Path B only)

```bash
mkdir -p ~/py39bin                                  # a private folder
ln -sf /usr/bin/python3.9 ~/py39bin/python3         # in it, "python3" means 3.9
ln -sf /usr/bin/python3.9 ~/py39bin/python
```

This only takes effect in a terminal where you run `export PATH=~/py39bin:$PATH`, so the rest of Ubuntu keeps using 3.8.

### 2.3 Clone ArduPilot and install its prerequisites

```bash
mkdir -p ~/ardupilot_esp32_build && cd ~/ardupilot_esp32_build
git clone --recursive https://github.com/ArduPilot/ardupilot.git    # several GB
cd ardupilot
git submodule status | grep -E '^[-+U]'             # must print nothing
Tools/environment_install/install-prereqs-ubuntu.sh -y
```

Then **close the terminal and open a new one**. If the clone was interrupted, delete the `ardupilot` folder and clone again.

### 2.4 Fetch ESP-IDF 5.3

```bash
export PATH=~/py39bin:$PATH                          # Path B only
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

### 2.5 Create the ESP-IDF Python environment

In a **fresh terminal**:

```bash
export PATH=~/py39bin:$PATH                          # Path B only
which python3                                        # A: /usr/bin/python3   B: ~/py39bin/python3
echo "VIRTUAL_ENV=$VIRTUAL_ENV"                      # must be empty
cd ~/ardupilot_esp32_build/ardupilot
./modules/esp_idf/install.sh                         # ends with "All done!"
```

**Path B only:** pin two packages whose newest versions Python 3.9 can't find:

```bash
~/.espressif/python_env/idf5.3_py3.9_env/bin/python -m pip install \
  "ruamel.yaml==0.18.15" "ruamel.yaml.clib==0.2.14"
```

### 2.6 The `get_idf` shortcut

Every new terminal needs the ESP32 tools loaded before building. Add **one** of these lines to the end of `~/.bashrc`, then open a new terminal:

```bash
# Path A (22.04+):
alias get_idf='source ~/ardupilot_esp32_build/ardupilot/modules/esp_idf/export.sh'
# Path B (20.04): the shim must come first
alias get_idf='export PATH=~/py39bin:$PATH; source ~/ardupilot_esp32_build/ardupilot/modules/esp_idf/export.sh'
```

Then, once, add ArduPilot's own Python modules to the same environment:

```bash
cd ~/ardupilot_esp32_build/ardupilot
get_idf                                              # load the tools
python3 -m pip install empy==3.3.4 pexpect           # once per environment
```

**Check:** `get_idf` prints `Python requirements are satisfied.` and ends with `Done!`; `which python3` points into `~/.espressif/python_env/idf5.3_py3.x_env/bin/`; and `python3 -c "import esp_idf_monitor, em, pexpect; print('OK')"` prints `OK`.

Ignore three things in the `get_idf` output: the cmake "recommended 3.24" warning, the "run idf.py build" hint (ArduPilot always builds with `./waf`), and paths listed twice (you ran it twice in one terminal).

## Part 3: Create your board definition

The board definition (`hwdef.dat`) is the wiring diagram you hand ArduPilot: it says which pin does what. You'll copy ArduPilot's `esp32buzz` board and change only its name and WiFi details, so its proven pin map stays intact.

```bash
cd ~/ardupilot_esp32_build/ardupilot
cp -r libraries/AP_HAL_ESP32/hwdef/esp32buzz libraries/AP_HAL_ESP32/hwdef/esp32lukas   # your own copy
nano libraries/AP_HAL_ESP32/hwdef/esp32lukas/hwdef.dat                                  # edit it
```

Change only these three lines (the password needs at least 8 characters; the default `ardupilot123` lets anyone nearby join):

```
define HAL_ESP32_BOARD_NAME "esp32-lukas"
define WIFI_SSID "your-drone-name"
define WIFI_PWD "your-password"
```

Save with `Ctrl+O`, `Enter`, then exit with `Ctrl+X`.

### What the important lines mean

Leave these as they are; they already match the wiring in Part 7.

| hwdef line | What it sets |
| --- | --- |
| `ESP32_SPIBUS VSPI_HOST 1 GPIO_NUM_23 GPIO_NUM_19 GPIO_NUM_18` | SPI bus: MOSI 23, MISO 19, clock 18 |
| `ESP32_SPIDEV mpu9250 ... GPIO_NUM_5 ...` | MPU9250 chip select on GPIO5 |
| `ESP32_SPIDEV bmp280 ... GPIO_NUM_26 ...` | BMP280 chip select on GPIO26 |
| `ESP32_I2CBUS I2C_NUM_0 GPIO_NUM_13 GPIO_NUM_12 ...` | Spare I2C bus, unused here |
| `ESP32_RCOUT GPIO_NUM_25` … | Motor outputs: GPIO25, 27, 33, 32 (plus 22, 21 spare) |
| `define HAL_ESP32_RCIN GPIO_NUM_4` | RC (PPM) input on GPIO4 |
| `ESP32_SERIAL UART_NUM_0 GPIO_NUM_3 GPIO_NUM_1` | **SERIAL0**: the USB port, for Mission Planner |
| `ESP32_SERIAL UART_NUM_1 GPIO_NUM_16 GPIO_NUM_17` | **SERIAL3**: GPIO16 (RX) / GPIO17 (TX), for the MTF-01 |
| `define HAL_ESP32_WIFI 1` | **SERIAL1**: WiFi access point, MAVLink on TCP port 5760 |
| `ESP32_ADC_PIN ADC1_GPIO35_CHANNEL ...` | Analog input for the optional battery monitor |

### Why the MTF-01 port is SERIAL3, not SERIAL1

ArduPilot numbers its ports in a fixed order: SERIAL0 is USB, SERIAL1 is WiFi, SERIAL2 points to a third UART this board doesn't define, and SERIAL3 points to the second UART line, GPIO16/17. ArduPilot's README calls those pins "uart1"; that's the ESP32's own numbering, not ArduPilot's. Analogy: "uart1" is the building's internal room number, but ArduPilot delivers by flat number, and that room is flat 3.

**Rule for future edits:** change one thing at a time, rebuild, and confirm it works before the next change.

### 3.1 Create the board file in one step (recommended)

Instead of editing by hand (easy to add a line twice), create the board file from `esp32buzz` with one command that makes exactly three changes: the board name, the WiFi details, and the **compass**. Change the WiFi name and password in the command to your own (password at least 8 characters).

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

### 3.2 Why the compass lines are needed

The GY-91's compass (AK8963) sits **inside** the MPU9250 and can only be read through it. `esp32buzz` never declares it; its generic "probing" only looks for separate compass chips on the I2C bus, so it never finds the AK8963 and Mission Planner reports `Compass 1 not healthy`. ArduPilot's own `esp32nick` board, which uses the same pins as yours, declares it with exactly these two lines:

| Line | Meaning |
| --- | --- |
| `COMPASS AK8963:probe_mpu9250 0 ROTATION_NONE` | Read the AK8963 through the first MPU9250; same orientation as the board |
| `define AP_COMPASS_AK8963_ENABLED TRUE` | Build the AK8963 driver into the firmware |

Each line must appear **once**. A second copy stops the build with `Error: Duplicate MAG`.

### 3.3 Bake your parameters into the firmware (`defaults.parm`)

An ESP32 board folder can hold a `defaults.parm` file. The build embeds it in the firmware, and ArduPilot uses those values for every parameter that has never been saved. After an `erase_flash`, the board starts with all your settings already in place, so you don't type them into Mission Planner. ArduPilot's own M5StampFly board works this way.

Create the file:

```bash
nano ~/ardupilot_esp32_build/ardupilot/libraries/AP_HAL_ESP32/hwdef/esp32lukas/defaults.parm
```

Paste this, save (`Ctrl+O`, `Enter`) and exit (`Ctrl+X`):

```
# --- loop rate: the ESP32 cannot run 400 Hz
SCHED_LOOP_RATE 150

# --- frame: quad X, plain PWM ESCs
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

# --- EKF3 sources for indoor flight
EK3_SRC1_POSXY 0
EK3_SRC1_VELXY 5
EK3_SRC1_POSZ 1
EK3_SRC1_VELZ 0
EK3_SRC1_YAW 1
```

Text after `#` is a comment. Don't add `SERIAL3_OPTIONS 1024`: on 4.8 firmware it blocks arming (see 11.4).

### 3.4 Check the board files before building

Two checks catch mistakes in minutes instead of after a 20-minute build and a flash.

**Board file:** run ArduPilot's own board-file checker. It must end with `Writing hwdef setup`.

```bash
cd ~/ardupilot_esp32_build/ardupilot
python3 libraries/AP_HAL_ESP32/hwdef/scripts/esp32_hwdef.py \
  libraries/AP_HAL_ESP32/hwdef/esp32lukas/hwdef.dat --outdir /tmp
```

**Parameter names:** a misspelt name in `defaults.parm` is silently ignored. This generates ArduCopter's full parameter list from the source and compares every line against it:

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

## Part 4: Build the firmware

Build the stock `esp32buzz` board first. If that works, your toolchain is proven, and any later error comes from your own changes.

```bash
cd ~/ardupilot_esp32_build/ardupilot
get_idf                                    # load the ESP32 tools
./waf configure                            # plain configure, once on a fresh checkout
./waf configure --board=esp32buzz          # choose the stock board
./waf copter                               # build ArduCopter (10–30 min the first time)
```

Then switch to your own board:

```bash
./waf configure --board=esp32lukas         # choose your board
./waf copter                               # build it
```

**Check:** `configure` prints `Setting board to : esp32lukas`, the build prints `Entering directory .../build/esp32lukas`, and it ends with `'copter' finished successfully`.

### Which board gets built

`./waf copter` always builds the board from the **last** `./waf configure --board=...` you ran, like a GPS that keeps driving to the last destination you set. Each board has its own folder (`build/esp32buzz/`, `build/esp32lukas/`), so files never mix. After building a different board, always run `./waf configure --board=esp32lukas` again, and check the `Entering directory` line.

### Rules

- **Never use `./waf build`.** It's broken for ESP32; always use `./waf copter`.
- **Rebuild after every board-file change**, starting with `./waf configure --board=esp32lukas`.
- **If the build fails,** the summary at the bottom only lists failed files. Save the log and find the first real error:

  ```bash
  ./waf copter -j1 > build_log.txt 2>&1      # one file at a time, everything into a file
  grep -n -m3 "error" build_log.txt          # show the first errors
  ```

### What gets produced

The files you flash are in `build/esp32lukas/esp-idf_build/`:

| File | Contents |
| --- | --- |
| `bootloader/bootloader.bin` | ESP32 bootloader |
| `partition_table/partition-table.bin` | Flash layout |
| `ardupilot.bin` | The ArduCopter firmware |
| `flash_args` | The exact flash address for each file, so you never type addresses yourself |

### Confirm the compass and defaults made it into the build

```bash
cd ~/ardupilot_esp32_build/ardupilot
grep -c "AK8963" build/esp32lukas/hwdef.h                        # 1 or more: compass driver included
grep -c "defaults.parm" build/esp32lukas/ap_romfs_embedded.h     # 1: defaults file embedded
grep -o "HAL_PARAM_DEFAULTS_PATH[^ ]*" -m1 build/esp32lukas/compile_commands.json   # firmware told to load it
```

**Check:** the first two print a number above 0, and the third prints `HAL_PARAM_DEFAULTS_PATH=\"@ROMFS/defaults.parm\"`. The build ends with a line like `ardupilot.bin binary size 0x1fafa0 bytes ... 34% free`.

## Part 5: Flash the ESP32

Flash the **bare** board, with only the USB cable connected. Wired peripherals can hold boot pins in the wrong state and stop flashing.

Pick one option:

| Option | Use it when |
| --- | --- |
| A: native Ubuntu | Ubuntu is installed directly on the PC |
| B: WSL2 + usbipd | You build in WSL2 and want to flash from Linux |
| C: Windows esptool | You build in WSL2 but would rather flash from Windows |

### Option A: native Ubuntu

```bash
ls /dev/ttyUSB*                       # usually /dev/ttyUSB0
sudo usermod -aG dialout $USER        # once; then log out and back in
cd ~/ardupilot_esp32_build/ardupilot
get_idf
esptool.py --chip esp32 -p /dev/ttyUSB0 erase_flash       # first flash only
ESPPORT=/dev/ttyUSB0 ESPBAUD=921600 ./waf copter --upload
```

`--upload` builds if needed, then flashes the board from your last `configure`, reading the addresses from `flash_args`. **Check:** it ends with `Hash of data verified.` and `Hard resetting`.

Every flash after the first only needs the last line (after `get_idf` in a new terminal).

### Option B: WSL2 with usbipd

WSL2 can't see USB devices by itself; usbipd lends one from Windows to Linux, and you hand it back when you're done.

**1. Attach the board to WSL.** In **PowerShell** on Windows:

```powershell
winget install --interactive --exact dorssel.usbipd-win   # once; Admin PowerShell, then reopen PowerShell
usbipd list                                               # find the board: 10c4:ea60 (CP210x) or 1a86:7523 (CH340); note its BUSID, e.g. 1-1
usbipd bind --busid 1-1                                   # once per board; Admin PowerShell; state -> Shared
usbipd attach --wsl --busid 1-1                           # every time you plug in; keep Ubuntu open; state -> Attached
```

**2. Check Linux can see it, then flash.** In **Ubuntu**:

```bash
lsusb | grep -i -E "10c4|1a86"        # shows the CP210x or CH340 chip
ls /dev/ttyUSB*                       # usually /dev/ttyUSB0
```

Now flash exactly as in Option A.

**3. Give the board back to Windows** (for Mission Planner or Option C). In **PowerShell**:

```powershell
usbipd detach --busid 1-1             # back to Windows; state -> Shared
usbipd unbind --busid 1-1             # Admin PowerShell; WSL stops claiming it; state -> Not shared
usbipd list                           # check the state; the COM port is back in Device Manager
```

Next time you flash from WSL, start again at `usbipd bind`. Leave every other device in `usbipd list` (mouse, keyboard, webcam) as **Not shared**; attaching one takes it away from Windows.

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

To browse the build folder in File Explorer instead, run `explorer.exe .` from that Ubuntu folder.

**2. Flash.** In **PowerShell**, replacing `COM5` with your port from Device Manager → Ports (it shows as Silicon Labs CP210x or USB-SERIAL CH340):

```powershell
winget install Python.Python.3.12                         # once, if "py --version" doesn't work; then reopen PowerShell
py -m pip install esptool                                 # once
cd C:\esp32flash
py -m esptool --chip esp32 -p COM5 erase_flash            # first flash only
py -m esptool --chip esp32 -p COM5 -b 921600 write_flash "@flash_args"
```

After every rebuild, repeat step 1 before flashing.

### If flashing won't start: manual download mode

`Failed to connect`, or `Unable to verify flash chip connection` with a crash message, means the board didn't enter flash mode, often because running firmware is crashing. Put it in flash mode by hand and tell esptool not to reset it:

```bash
# 1. hold BOOT, press and release EN, release BOOT
cd ~/ardupilot_esp32_build/ardupilot/build/esp32lukas/esp-idf_build
esptool.py --chip esp32 -p /dev/ttyUSB0 --before no_reset erase_flash
# 2. hold BOOT, press and release EN, release BOOT again
esptool.py --chip esp32 -p /dev/ttyUSB0 --before no_reset -b 460800 write_flash @flash_args
# 3. press EN to start the firmware
```

Still failing? Try another USB cable (many are charge-only) and lower speed (`-b 115200`).

### Check that it boots

```bash
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200      # quit with Ctrl+]
```

Press **EN**. You should see `Init ArduCopter`, a list of `OK created task` lines, and `WiFi softAP init finished`. `Firmware change: erasing EEPROM...` is **normal on the first boot after flashing only**. With nothing wired, IMU errors are expected at this stage.

### When to erase the flash again

Saved parameters always win over the built-in `defaults.parm`. So after you change `defaults.parm`, or when old settings are confusing things (for example a leftover `SCHED_LOOP_RATE = 80`), run `erase_flash` before flashing. Erasing also wipes calibration, so redo Part 12 afterwards.

## Part 6: Prove the board is stable before wiring everything

The Super Mini taught an expensive lesson: what looked like a WiFi problem was really the firmware **crashing and restarting**. Test for that now, with as little connected as possible, so a crash can't be blamed on wiring.

Do these tests with the board alone first, then again with **only the GY-91** wired (Part 7 shows its pins).

### 6.1 USB connection and full parameter load

The board must be in Windows' hands (detached from WSL). In Mission Planner, pick the board's COM port, set **115200**, click **Connect**.

**Check:** the parameter download completes (the progress bar reaches the end) and the HUD appears.

### 6.2 Settings survive a restart

1. In **Config → Full Parameter List**, set `FRAME_CLASS` to `1` and click **Write Params**.
2. Press **EN** on the board, wait about 10 seconds, and reconnect.
3. Check `FRAME_CLASS`.

**Check:** it still says `1`. If it's back to `0`, settings are being lost on every restart.

### 6.3 Ten minutes over WiFi

Disconnect USB from Mission Planner (keep the cable for power). Join the board's WiFi network, choose **TCP** in Mission Planner, connect to **192.168.4.1**, port **5760**, and leave it connected for 10 minutes.

**Check:** the parameters load fully and the link stays up.

### 6.4 If anything drops: catch the crash

When an ESP32 crashes, it prints the reason just before it restarts. ESP-IDF's monitor catches that and translates it into ArduPilot file names and line numbers. In Ubuntu, with the board attached to WSL:

```bash
cd ~/ardupilot_esp32_build/ardupilot
get_idf
ls build/esp32lukas/esp-idf_build/*.elf                    # find the firmware's .elf file
python3 -m esp_idf_monitor -p /dev/ttyUSB0 -b 115200 \
  build/esp32lukas/esp-idf_build/ardupilot.elf             # use the name ls showed; quit with Ctrl+]
```

Leave it running and wait for the drop. Note: while the board is attached to WSL, Mission Planner can only use WiFi.

### 6.5 What the result means

| What you see | Meaning | Next step |
| --- | --- | --- |
| All three checks pass | Board is stable | Carry on to Part 7 |
| `Guru Meditation Error` with decoded lines | Firmware crash | Note the file and function names shown; report them on the ArduPilot forum's ESP32 thread |
| `Brownout detector was triggered` | Power dip | Power from a good 5V supply; add 100–470 µF across 5V and GND |
| No crash, but WiFi drops | Link problem, not the board | See Part 10.3 |
| USB stable, WiFi crashes | Problem in ArduPilot's ESP32 WiFi code | Use USB for setup; WiFi for monitoring only |
| `Firmware change: erasing EEPROM` on every boot | Board restarting before it can save | Treat as a crash; catch it with 6.4 |

# Section 2: Schematic and wiring

By the end of this section every device is wired to the right ESP32 pin and set up correctly on its own side: the GY-91 chip checked, the receiver bound with its failsafe set, and the MTF-01 talking MAVLink.

## Part 7: Wire everything

Wire with the battery **disconnected** and props **off**. Add devices one at a time in this order, checking each in Mission Planner before the next: GY-91, then receiver, then MTF-01, then ESCs.

&#91;embedded content: ESP32 DevKit wiring · GY-91, FS-A8S, MTF-01, ESCs, BEC\]

### Pin table

| Device pin | ESP32 pin | Notes |
| --- | --- | --- |
| GY-91 SCL | GPIO18 | Works as SPI clock |
| GY-91 SDA | GPIO23 | Works as SPI MOSI |
| GY-91 SDO/SAO | GPIO19 | Works as SPI MISO |
| GY-91 NCS | GPIO5 | MPU9250 chip select |
| GY-91 CSB | GPIO26 | BMP280 chip select |
| GY-91 VIN / GND | 3V3 / GND | Power through VIN only; leave the GY-91's own 3V3 pin unconnected |
| FS-A8S PPM | GPIO4 | Use the PPM pad, not iBUS or S.BUS |
| FS-A8S + / − | 5V / GND | Needs 4.0–8.4V |
| MTF-01 TX | GPIO16 | ESP32 RX, ArduPilot SERIAL3 |
| MTF-01 RX | GPIO17 | ESP32 TX |
| MTF-01 VCC / GND | 5V / GND |  |
| ESC 1–4 signal | GPIO25, 27, 33, 32 | Motors 1–4 |
| ESC signal ground | GND | Needed for a clean PWM signal |
| BEC 5V / GND | 5V (VIN) / GND | Main supply |

**Why the GY-91 pins say SCL/SDA.** Its chips speak both I2C and SPI through the same pins, and the maker printed the I2C names. Pulling NCS or CSB low switches each chip to SPI, where SCL is the clock and SDA is MOSI. SPI runs at up to 8 MHz versus 400 kHz for I2C, which matters because the IMU is read thousands of times a second.

### Power and ground

- **One 5V rail, one ground rail.** Run the BEC to a small 5V bus and ground bus, and connect every device there.
- **ESCs take battery power directly.** Only their signal wire and ground go to the ESP32.
- **USB or BEC, not both,** unless you've confirmed your DevKit has a diode between USB 5V and VIN. Use USB on the bench (no battery) and the BEC when the battery is in.

### Pins to leave alone

- **GPIO12 and GPIO13** (spare I2C bus): a pull-up on GPIO12 at power-on stops the board booting.
- **GPIO0, 2, 15** (boot-strap pins) and **GPIO6–11** (internal flash).

### Check before powering up

- **Measure the FS-A8S PPM signal** with a multimeter before connecting it to GPIO4. ESP32 inputs are 3.3V-only; use a resistor divider if it's higher.
- **Check for shorts** between 5V and GND before the first power-up.

### Motor layout (quad X, viewed from above, front at top)

| Motor | ESP32 pin | Position | Spin |
| --- | --- | --- | --- |
| 1 | GPIO25 | Front right | Counter-clockwise |
| 2 | GPIO27 | Rear left | Counter-clockwise |
| 3 | GPIO33 | Front left | Clockwise |
| 4 | GPIO32 | Rear right | Clockwise |

Mount the ESP32 and GY-91 flat near the frame centre on soft foam, GY-91 arrow forward, with the ESP32's antenna end clear of metal, carbon fibre and wires.

## Part 8: Optional extras

Add these once the basic build works.

### 8.1 Battery voltage monitor

Without it, ArduPilot can't warn you or land when the battery is low. The ESP32's analog input reads up to about 3.1V, so use a resistor divider:

```
Battery + ──[ 10 kΩ ]──┬── GPIO35
                       │
                   [ 2.2 kΩ ]   (plus 100 nF across it to steady the reading)
                       │
Battery − ─────────────┴── GND
```

This divides by about 5.55: a full 4S (16.8V) becomes about 3.0V, a full 3S (12.6V) about 2.3V.

| Parameter | Value | Meaning |
| --- | --- | --- |
| `BATT_MONITOR` | 3 | Analog voltage only |
| `BATT_VOLT_PIN` | 35 | ArduPilot's pin number for GPIO35 on this board |
| `BATT_VOLT_MULT` | 5.55 | Starting value; calibrate below |
| `BATT_LOW_VOLT` | 3.5 × cells | Low-battery threshold |
| `BATT_FS_LOW_ACT` | 1 | Land when low (return-to-launch needs GPS) |

**Calibrate:** measure the battery with a multimeter and set `BATT_VOLT_MULT = old value × (multimeter reading ÷ reported voltage)`.

### 8.2 SD card logging

Flight logs are the main tool for diagnosing vibration, tuning and crashes. The board definition uses an SD card in 1-bit mode:

| SD card pin | ESP32 pin | Note |
| --- | --- | --- |
| CLK | GPIO14 |  |
| CMD | GPIO15 | 10 kΩ pull-up to 3.3V |
| D0 | GPIO2 | 10 kΩ pull-up to 3.3V |
| VCC / GND | 3.3V / GND |  |

- **Use a plain 3.3V microSD breakout.** The common "SPI microSD module" with a 5V level-shifter chip blocks the two-way signals this mode needs.
- **Unplug the SD module while flashing.** Its pull-up on GPIO2 can stop the ESP32 entering flash mode.
- **No SD card?** If arming fails with `PreArm: Logging failed`, set `LOG_BACKEND_TYPE = 0` for bench testing.

## Part 9: Prepare the peripherals

### 9.1 Check which chip your GY-91 has

Many cheap GY-91 boards carry an MPU6500 (no magnetometer) or a clone ArduPilot won't recognise. In the Arduino IDE (ESP32 board package, board **ESP32 Dev Module**), wire the GY-91 as in Part 7, upload this, and open the Serial Monitor at 115200:

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

| IMU reading | Chip | Meaning |
| --- | --- | --- |
| `0x71` / `0x73` | MPU9250 / MPU9255 | Good, with magnetometer |
| `0x70` | MPU6500 | Works, **no magnetometer** (see 11.6) |
| `0x00` or `0xFF` | No answer | Check wiring and power |
| anything else | Clone | ArduPilot may not detect it |

**Compass check.** The compass (AK8963) has no pins of its own on the GY-91. It is a second die inside the MPU9250 package, wired to the MPU's internal I2C bus at address `0x0C`. The ESP32 can only reach it through the MPU, so a normal I2C scanner never finds it. The sketch asks the MPU to read the compass ID and reports whether anything answered at `0x0C`.

A good board prints:

```
IMU WHO_AM_I: 0x71   BARO ID: 0x58   MAG @0x0C: FOUND   MAG ID: 0x48
```

| MAG reading | Meaning | Fix |
| --- | --- | --- |
| `FOUND`, `0x48` | Compass alive | None. If ArduPilot still shows no compass, check 3.2 and `COMPASS_DEV_ID` |
| `NOT FOUND`, `0x00` (IMU `0x71`) | Compass not answering: its internal bus is stuck, or the compass die is dead | Power-cycle, below |
| IMU `0x70` | MPU6500, no compass inside | 11.6 |

**If `NOT FOUND`:** pressing EN or reflashing does not cut power to the GY-91, so a compass interrupted mid-transfer can keep holding its bus.

```
1. Unplug USB for 10 s                  # also the GY-91's own supply, if it has one
2. Plug in, read the Serial Monitor     # sketch is still on the board
   -> FOUND, 0x48  = bus was stuck, fixed
   -> NOT FOUND    = go to 3
3. Reflash ArduPilot, power-cycle, read COMPASS_DEV_ID
   -> 0 as well    = compass die is dead: replace the GY-91 or fit an external I2C compass
```

The barometer should read `0x58`. This sketch replaces ArduPilot, so **reflash** (Part 5) afterwards, then unplug and replug USB once.

### 9.2 FlySky FS-i6 and FS-A8S

- **Bind:** hold the FS-A8S bind button while powering it, then switch on the FS-i6 holding **BIND KEY**. Power-cycle both when the receiver LED goes solid.
- **PPM output:** if GPIO4 later sees no signal, set **System → RX setup → Output mode → PPM** on the FS-i6.
- **Switches:** **Functions setup → Aux. channels**: Channel 5 = **SwC** (flight modes), Channel 6 = **SwA** (emergency stop).

**Throttle failsafe (essential).** By default, FlySky receivers keep repeating the last stick values when the link drops. Make the receiver send a throttle value below anything the stick can produce:

1. **Functions setup → End points → Channel 3**: set the low end to **120%**.
2. **Functions setup → Failsafe → Channel 3**: hold the throttle stick fully down and long-press **Down** to store it.
3. **Functions setup → End points → Channel 3**: set the low end back to **100%**.

The failsafe value is now about 900 µs, below ArduPilot's `FS_THR_VALUE` of 975. You'll verify it in Part 12.

### 9.3 MicoAir MTF-01

Configure it **before** connecting it to the ESP32, using the USB-to-TTL adapter and MicoAir's **MicoAssistant**:

| Setting | Value | Why |
| --- | --- | --- |
| Output protocol | `mav_apm` | MAVLink format ArduPilot understands |
| `mav_id` (firmware 4.5+) | `200` | Anything except 1, which is the flight controller's own ID |
| Baud | 115200 | Matches `SERIAL3_BAUD = 115` |

Mount it pointing straight down, arrow forward, with a clear view of the floor. Optical flow needs a textured floor and decent light; plain white or glossy floors give poor readings.

# Section 3: ArduPilot parameter settings

By the end of this section Mission Planner is connected, every parameter is set, the sensors and radio are calibrated, and the frame has passed a props-off bench test.

## Part 10: Connect Mission Planner

### 10.1 Over USB (best for setup)

The DevKit's USB port goes through its CP2102/CH340 chip to ArduPilot's SERIAL0, so Mission Planner works over the cable.

1. If you used WSL to flash, give the board back to Windows first (Part 5, Option B, step 3).
2. In Mission Planner (top right), choose the board's **COM port** and baud **115200**, then click **Connect**.

**Set Mission Planner's reset options first (once).** When Mission Planner opens the COM port, it can toggle the USB chip's DTR and RTS lines. On an ESP32 DevKit those lines drive the **EN (reset)** and **BOOT** pins, so connecting can reboot the board, and Mission Planner then waits while it boots and the EKF starts. This makes connecting take a very long time, or fail.

1. In Mission Planner, go to **Config → Planner** and find the connect/reset options.
2. Tick only **Reset on USB Connect (toggle DTR)** and untick the RTS option.
3. If connecting is still slow, untick both.

**Check:** clicking **Connect** reaches the parameter download within a few seconds, and the board doesn't reboot (its WiFi network doesn't disappear) when you connect.

### 10.2 Over WiFi (good for monitoring)

1. Join the WiFi network named in your board file (`WIFI_SSID`), password `WIFI_PWD`.
2. In Mission Planner, choose **TCP**, click **Connect**, and enter host **192.168.4.1**, port **5760**.

`192.168.4.1` is the ESP32's default address as a WiFi access point; ArduPilot's ESP32 README lists exactly this address and port. To confirm, run `ipconfig` in PowerShell while connected: the **Default Gateway** is the ESP32.

### 10.3 If WiFi drops or parameters never finish loading

Check these in order:

1. **Is the board crashing?** If the WiFi network disappears from the list for a few seconds, the board restarted. Catch the crash with Part 6.4.
2. **Is Windows switching networks?** The drone network has no internet, so Windows may hop back to your home WiFi mid-download. In Windows WiFi settings, untick **Connect automatically** on your home network while you work.
3. **Antenna blocked?** Keep the ESP32's antenna end clear of metal, breadboards (many have a metal backing plate), carbon fibre and wires.
4. **Weak power?** Power from a good 5V supply rather than a PC USB port, and add a 100–470 µF capacitor across 5V and GND.
5. **Transmit power too high for the regulator** (a common fix on small ESP32 boards). ArduPilot doesn't limit WiFi power, but this tested patch adds an optional limit:

   ```bash
   cd ~/ardupilot_esp32_build/ardupilot
   F=libraries/AP_HAL_ESP32/WiFiDriver.cpp
   L=$(grep -n "esp_wifi_start());" $F | head -1 | cut -d: -f1)    # find the WiFi start line
   sed -i "${L}a\\
   #ifdef WIFI_MAX_TX_POWER\\
       esp_wifi_set_max_tx_power(WIFI_MAX_TX_POWER);\\
   #endif" $F                                                       # add the limit after it
   grep -n -A3 "esp_wifi_start());" $F                              # check the 3 new lines
   echo "define WIFI_MAX_TX_POWER 34" >> libraries/AP_HAL_ESP32/hwdef/esp32lukas/hwdef.dat   # 34 = 8.5 dBm
   ```

   Rebuild and flash. Range drops to a few metres. This edits an ArduPilot source file, so run `git stash` before `git pull` and `git stash pop` after.

Whatever the cause, use **USB for parameter loading and calibration**, and WiFi only for monitoring.

### 10.4 First-connection checks

- **Messages tab** (Data → Messages): the IMU and barometer are detected. Some `PreArm` messages are normal until calibration is done.
- **HUD:** tilt the board and the horizon follows smoothly.
- **Full Parameter List** (Config) loads completely.

## Part 11: Set the ArduPilot parameters

In **Config → Full Parameter List**, change one group at a time, click **Write Params**, then **reboot** the board (press EN). Several settings only take effect after a reboot. Use USB for this.

> **If you built with `defaults.parm` (3.3) and erased the flash, every value in this Part is already set.** Use the tables to check them in Mission Planner and to understand what each one does. You only need to set values by hand if you skipped 3.3.

### 11.0 Loop rate (set this first)

ArduCopter runs its control loop at 400 times a second by default, and since Copter 4.3.2 it refuses to arm if the board can't keep up. The ESP32 can't: on this build it measured about **229 Hz**, so arming fails with `PreArm: Main loop slow (229Hz < 400Hz)`.

| Parameter | Value | Meaning |
| --- | --- | --- |
| `SCHED_LOOP_RATE` | 200 | Run the control loop 200 times a second, a rate the ESP32 can actually hold |

Write it and reboot. This doesn't make the board faster; it sets an honest target. Analogy: the juggler can catch a ball every 4.4 ms but was told every 2.5 ms; now he's told every 5 ms, and he keeps up.

Rules for this setting:

- **Set it below the rate your board measures.** If yours reports less than 229 Hz, choose a round number below it (for example 150).
- **Expect softer control** than on a 400 Hz flight controller. Start with lower gains and fly gently.
- **Don't bypass the check** by disabling `ARMING_CHECK`; the warning reflects a real control-quality problem.
- **Reduce load** to keep headroom: use the USB link rather than WiFi for setup, and don't stream more telemetry than you need.

### 11.1 Frame and motors

| Parameter | Value | Meaning |
| --- | --- | --- |
| `FRAME_CLASS` | 1 | Quad |
| `FRAME_TYPE` | 1 | X layout |
| `MOT_PWM_TYPE` | 0 | Normal PWM (no DShot on this port) |
| `MOT_PWM_MIN` / `MOT_PWM_MAX` | 1000 / 2000 | PWM range sent to the ESCs |
| `SERVO1_FUNCTION` … `SERVO4_FUNCTION` | 33, 34, 35, 36 | Motors 1–4 on GPIO25, 27, 33, 32 |
| `AHRS_ORIENTATION` | 0 | GY-91 arrow forward; change only if mounted differently |

### 11.2 Radio and flight modes

| Parameter | Value | Meaning |
| --- | --- | --- |
| `RCMAP_ROLL` / `PITCH` / `THROTTLE` / `YAW` | 1 / 2 / 3 / 4 | Matches the FS-i6's channel order (the defaults) |
| `FLTMODE_CH` | 5 | Flight modes come from channel 5 (SwC) |
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
| `SERIAL3_PROTOCOL` | 1 | MAVLink on GPIO16/17. SERIAL3 defaults to GPS, so this **must** change |
| `SERIAL3_BAUD` | 115 | 115200 baud |
| `SERIAL3_OPTIONS` | 1024 | Don't forward MAVLink traffic to the sensor |
| `FLOW_TYPE` | 5 | Optical flow from MAVLink |
| `RNGFND1_TYPE` | 10 | Rangefinder from MAVLink |

> **Don't set `SERIAL3_OPTIONS = 1024` on this firmware.** Older guides (including an earlier version of this one) use it to stop MAVLink being forwarded to the sensor. From ArduPilot 4.7 that option moved to `MAVn_OPTIONS`, and 4.8 refuses to arm with `SERIAL3_OPTIONS bit 10 ('no forward mavlink') must not be set`. The MTF-01 works without it. Ignore the `SERIAL3_OPTIONS` row above; if you set it earlier, put it back to `0`.

Reboot, then:

| Parameter | Value | Meaning |
| --- | --- | --- |
| `RNGFND1_MAX` / `RNGFND1_MIN` | 8 / 0.01 | Range in metres |
| `RNGFND1_ORIENT` | 25 | Pointing down (default) |
| `FLOW_ORIENT_YAW` | 0 | MTF-01 arrow forward |

### 11.5 Position estimate (EKF3) for indoor flight

| Parameter | Value | Meaning |
| --- | --- | --- |
| `EK3_SRC1_POSXY` | 0 | No absolute position (no GPS) |
| `EK3_SRC1_VELXY` | 5 | Horizontal velocity from optical flow |
| `EK3_SRC1_POSZ` | 1 | Height from the barometer |
| `EK3_SRC1_VELZ` | 0 | No vertical velocity source |
| `EK3_SRC1_YAW` | 1 | Heading from the compass |

After the first hovers, follow ArduPilot's **Optical Flow** setup page to calibrate `FLOW_FXSCALER` / `FLOW_FYSCALER`.

### 11.6 Compass

- **MPU9250/9255** (`0x71`/`0x73` in 9.1): ArduPilot probes for its built-in magnetometer. If a compass appears, keep `COMPASS_ENABLE = 1` and calibrate it.
- **MPU6500** (`0x70`) or no compass found: set `COMPASS_ENABLE = 0`, fly only Stabilize and AltHold, and read ArduPilot's non-GPS, compass-less guidance before using Loiter.

Indoors, steel and power cables distort any compass, so treat its heading with suspicion.

## Part 12: Calibrate

In Mission Planner's **Setup → Mandatory Hardware**, over USB, with **props off**. Reboot after each.

### 12.1 Accelerometer

**Calibrate Accel**: hold the frame still in each of the six positions it asks for, then **Calibrate Level** with the frame sitting level. **Check:** the HUD horizon is level when the frame is level.

### 12.2 Compass (only if detected)

**Start**, then rotate the frame slowly through every orientation until the bar completes. Do it outdoors, away from cars, steel tables and cables. **Check:** the HUD heading changes correctly as you turn the frame.

### 12.3 Radio

**Calibrate Radio**: move both sticks to every corner and every switch through every position, then **Click when Done**.

| Action | Bar that should move |
| --- | --- |
| Right stick left/right | Roll (channel 1) |
| Right stick up/down | Pitch (channel 2); stick **up** shows the bar going **down**, which is correct |
| Left stick up/down | Throttle (channel 3) |
| Left stick left/right | Yaw (channel 4) |
| SwC | Channel 5, and the HUD flight mode changes |
| SwA | Channel 6 |

**Test the failsafe.** With the receiver powered, switch the transmitter **off**. **Check:** channel 3 drops to about 900, below 975, and Mission Planner reports a radio failsafe. If it stays at its last value, redo 9.2.

### 12.4 ESCs (props off, battery needed)

The ESCs must learn the 1000–2000 µs range so all four motors start and stop together.

1. Set `ESC_CALIBRATION = 3` and write it.
2. Unplug USB and the battery.
3. Plug in the battery; the ESCs play their calibration tones.
4. Unplug the battery and plug it in again. `ESC_CALIBRATION` resets itself.

Some ESCs (BLHeli\_S and similar) use a fixed range and skip this; their manual will say.

### 12.5 Flow sensor

Hold the frame 30–50 cm above a textured floor and open **Data → Status**:

| Value | What it should do |
| --- | --- |
| `rangefinder1` | Tracks height as you raise and lower the frame |
| `opt_qua` | Flow quality, well above 0 over texture |
| `opt_m_x`, `opt_m_y` | Change when you move the frame sideways and forwards |

If none appear, check `SERIAL3_PROTOCOL`, the TX/RX crossover, and the MTF-01's protocol and `mav_id`.

## Part 13: Bench test, then first flight

### 13.1 Bench test (props OFF)

Work through these in order. Each line rules out a cause before the next one depends on it.

- [ ] Boot shows the IMU and barometer detected, with no `INS: unable to initialise driver`
- [ ] HUD follows tilt smoothly, with no drift while still
- [ ] Barometer altitude changes when you lift the frame about 1 m
- [ ] Radio bars correct; SwC switches Stabilize / AltHold / Loiter
- [ ] Transmitter off triggers radio failsafe
- [ ] Flow and rangefinder values respond (12.5)
- [ ] **Motor Test** (Setup → Optional Hardware): test A = front right, B = rear right, C = rear left, D = front left; each spins the right way (swap two motor wires to reverse)
- [ ] Arm in Stabilize (throttle down, rudder right \~2 s): motors idle
- [ ] Tilt the frame by hand: the low-side motors speed up
- [ ] SwA emergency stop cuts the motors instantly
- [ ] Disarm (throttle down, rudder left \~2 s): motors stop

If arming is refused, read the `PreArm:` message in the HUD or Messages tab and fix that cause. Don't disable `ARMING_CHECK`.

**Props on** only when every line passes: front-right and rear-left take counter-clockwise props, front-left and rear-right take clockwise props.

### 13.2 First flight, in stages

Tether the frame (light cords to heavy weights at each corner) or fly inside a net, and keep everyone behind you.

| Stage | Mode | Goal | Move on when |
| --- | --- | --- | --- |
| 1 | Stabilize | Throttle up until light on its feet, then land | No flip, no strong drift |
| 2 | Stabilize | Hover at 30–50 cm for 10–20 s | Holds attitude with small corrections |
| 3 | AltHold | Switch from a Stabilize hover | Holds height within about 20 cm |
| 4 | Loiter | Over a textured floor | Holds position hands-off |

**Before every flight:**

- [ ] Battery charged, props tight and on the correct motors
- [ ] HUD level, no PreArm errors, mode switch on Stabilize
- [ ] Transmitter on **before** the battery; battery out **before** the transmitter off
- [ ] Finger on SwA (emergency stop)

**If something goes wrong:** cut the throttle or hit SwA. Don't try to save a flip at low height.

| What you see | Likely cause |
| --- | --- |
| Flips immediately on take-off | Wrong motor order, spin direction, or prop placement |
| Drifts steadily one way in Stabilize | Level not calibrated, or centre of gravity off |
| Fast wobble or oscillation | Gains too high, or vibration reaching the GY-91 |
| Climbs or sinks in AltHold | Prop wash on the barometer (cover it with open-cell foam) or vibration |
| Wanders in Loiter | Poor floor texture or light, uncalibrated flow, bad compass |

If you fitted an SD card, check the vibration (`VIBE`) in the logs before tuning; it should stay well below 30 m/s². Leave AutoTune until basic flight is solid.

# Appendix

## Part 14: Troubleshooting

Every error below was met during this project. Find your message, then apply the fix.

### Build environment

| Error | Cause | Fix |
| --- | --- | --- |
| `Aborting` / `Unable to checkout` in submodules | Stray files from an interrupted run | Submodule repair, 2.4 |
| `Python being checked: ...idf4.2_py3.8_env...` | Old `IDF_PYTHON_ENV_PATH` set | `grep -rn IDF_PYTHON_ENV_PATH ~/.bashrc ~/.profile ~/.bash_profile`, remove it, new terminal |
| `can not create a virtual environment again` | Terminal is inside another environment | New terminal; `echo $VIRTUAL_ENV` must be empty |
| `ensurepip is not available` | venv package missing | Install `python3.8-venv` / `python3-venv`, delete the half-made environment, rerun `install.sh` |
| `Python minimum supported version is 3.9.0` | Ubuntu 20.04 Python, or shim not first in PATH | Path B shim (2.2) and `get_idf` alias |
| `Package was not found ... ruamel.yaml` (or `.clib`) | Python 3.9 can't read the new package names | Pins in 2.5 |
| `Cannot import module "esp_idf_monitor"` | Wrong Python first in PATH | New terminal, then `get_idf` once |
| `no such option: -m` from pip | Two commands pasted on one line | Run them separately |
| `ninja: error: loading 'build.ninja'` | Skipped plain configure | `./waf configure`, then with `--board` |
| `Missing configuration file .../hwdef.h` | Configures run out of order | `./waf configure --board=esp32lukas` again, then build |
| `No module named em` | ArduPilot modules not in the IDF environment | `python3 -m pip install empy==3.3.4 pexpect` after `get_idf` |
| Many files fail at once | One shared setting is wrong | `./waf copter -j1 > build_log.txt 2>&1; grep -n -m3 "error" build_log.txt` |

### Flashing and boot

| Symptom | Cause | Fix |
| --- | --- | --- |
| `Failed to connect to ESP32` | Not in flash mode, or charge-only cable | Manual download mode (Part 5); another cable |
| `Unable to verify flash chip connection (Guru Meditation...)` | Running firmware is crashing | Manual download mode with `--before no_reset` |
| No `/dev/ttyUSB0` in WSL | Board not attached | `usbipd attach --wsl --busid <id>` |
| `Permission denied` on the port | Not in `dialout` group | `sudo usermod -aG dialout $USER`, log out and in |
| Board missing in Windows | Still attached to WSL | `usbipd detach` then `usbipd unbind` (Part 5, B3) |
| Boot loop | Bad flash, weak power, or GPIO12 pulled high | Erase and reflash; good power; free GPIO12 |
| Won't flash with SD module fitted | Pull-up on GPIO2 | Unplug the SD module while flashing |

### Running, connection and WiFi

| Symptom | Cause | Fix |
| --- | --- | --- |
| `erasing EEPROM` on every boot | Board restarts before saving | Treat as a crash; Part 6.4 |
| WiFi drops, network vanishes briefly | Board restarting | Part 6.4 to catch the reason |
| WiFi drops, network stays listed | Windows switched networks, or weak link | Part 10.3 |
| Parameters never finish over WiFi | Slow or dropping link | Use USB for setup (10.1) |
| Mission Planner can't connect over COM | Board attached to WSL, or wrong baud | Detach; 115200 |

**Mission Planner takes very long to connect, or keeps retrying:** it's resetting the board through the USB chip's DTR/RTS lines. In **Config → Planner**, tick only **Reset on USB Connect (toggle DTR)** and untick RTS; if still slow, untick both (see 10.1).

**`PreArm: Compass 1 not healthy`** (with a genuine MPU9250): the board file doesn't declare the AK8963. Add the two compass lines (3.2), rebuild, flash.

**`Error: Duplicate MAG`** during configure: a compass line is in `hwdef.dat` twice. Recreate the file with 3.1.

**`PreArm: SERIAL3_OPTIONS bit 10 ('no forward mavlink') must not be set`:** set `SERIAL3_OPTIONS = 0` (see 11.4).

### Sensors, radio and arming

| Symptom | Cause | Fix |
| --- | --- | --- |
| `INS: unable to initialise driver` | IMU wiring, power or clone chip | Check Part 7 wiring, VIN-only power, chip check 9.1 |
| No barometer | CSB wiring | CSB to GPIO26; chip ID `0x58` |
| No compass | MPU6500 board, or compass not answering (stuck bus or dead compass) | 9.1 compass check (unplug USB 10 s and retest); 11.6 |
| No RC input | iBUS pad, wrong pin, unbound or unpowered receiver | PPM pad to GPIO4, 5V power, rebind, PPM output on |
| Failsafe doesn't trigger | Receiver failsafe not set | Redo 9.2, re-test 12.3 |
| No flow or rangefinder data | Port setting, crossover, MTF-01 setup | `SERIAL3_PROTOCOL = 1`, TX→RX crossover, `mav_apm`, `mav_id = 200` |
| `PreArm: Logging failed` | No SD card | Fit one (8.2) or `LOG_BACKEND_TYPE = 0` for bench tests |
| `PreArm: RC not calibrated` / `Accels not calibrated` | Calibration not done | 12.3 / 12.1 |
| One motor doesn't spin | Signal wire, missing signal ground, ESC not calibrated | Check wiring; 12.4 |

**`PreArm: Main loop slow (229Hz < 400Hz)`:** the ESP32 can't run the control loop at the default 400 Hz. Set `SCHED_LOOP_RATE = 200` (or a round number below the rate shown), write, and reboot. See 11.0.

**When asking for help,** include the **first** error line or the exact `PreArm` message, your Ubuntu version, and which Part you were on.

## Quick reference

**Everyday build and flash** (new Ubuntu terminal):

```bash
cd ~/ardupilot_esp32_build/ardupilot
get_idf                                                   # load the tools
./waf configure --board=esp32lukas                        # only after building another board
ESPPORT=/dev/ttyUSB0 ESPBAUD=921600 ./waf copter --upload  # build and flash
```

**WSL USB cycle** (PowerShell):

```powershell
usbipd bind --busid 1-1                # Admin; once per board, or after unbind
usbipd attach --wsl --busid 1-1        # lend to Linux for flashing
usbipd detach --busid 1-1              # give back to Windows
usbipd unbind --busid 1-1              # Admin; stop sharing with WSL
```

**Pins**

| Function | GPIO |
| --- | --- |
| SPI clock / MOSI / MISO | 18 / 23 / 19 |
| MPU9250 CS / BMP280 CS | 5 / 26 |
| RC PPM input | 4 |
| Motors 1–4 (5–6 spare) | 25, 27, 33, 32 (22, 21) |
| MTF-01, SERIAL3 RX / TX | 16 / 17 |
| Battery voltage (optional) | 35 |
| SD card CLK / CMD / D0 (optional) | 14 / 15 / 2 |
| Leave free | 0, 6–11, 12, 13 |

**Ports**

| ArduPilot port | Link |
| --- | --- |
| SERIAL0 | USB cable, 115200 (Mission Planner) |
| SERIAL1 | WiFi, TCP 192.168.4.1 port 5760 |
| SERIAL2 | Not defined on this board |
| SERIAL3 | GPIO16/17, MTF-01 |

**Key parameters**

| Group | Settings |
| --- | --- |
| Frame | `FRAME_CLASS 1`, `FRAME_TYPE 1`, `MOT_PWM_TYPE 0` |
| Modes | `FLTMODE_CH 5`, `FLTMODE1 0`, `FLTMODE4 2`, `FLTMODE6 5`, `RC6_OPTION 31` |
| Failsafe | `FS_THR_ENABLE 3`, `FS_THR_VALUE 975`, `FS_GCS_ENABLE 0` |
| Flow | `SERIAL3_PROTOCOL 1`, `SERIAL3_BAUD 115`, SERIAL3\_OPTIONS 0, `FLOW_TYPE 5`, `RNGFND1_TYPE 10`, `RNGFND1_MAX 8`, `RNGFND1_MIN 0.01` |
| EKF | `EK3_SRC1_POSXY 0`, `EK3_SRC1_VELXY 5`, `EK3_SRC1_POSZ 1`, `EK3_SRC1_VELZ 0`, `EK3_SRC1_YAW 1` |

Set `SCHED_LOOP_RATE 200` before everything else; without it the board won't arm.

**Order of work:** check board → environment → board file → build → flash bare board → stability test → wire one device at a time → peripherals → Mission Planner over USB → parameters → calibration → bench test → props on → tethered hover.
