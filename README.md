# ⚽ Soccer Nopal - Roamer N' Destroyer

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3.2-blue.svg)](https://github.com/espressif/esp-idf)
[![Hardware](https://img.shields.io/badge/Hardware-ESP32--D0WD--V3-orange.svg)](https://www.espressif.com/)
[![PWM](https://img.shields.io/badge/PWM-20_kHz_Ultrasonic-green.svg)]()
[![FreeRTOS](https://img.shields.io/badge/RTOS-Deterministic_100Hz-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-blue.svg)]()

High-performance, ultra-low latency firmware for competitive differential-drive robot soccer running natively on **Espressif ESP-IDF v5.3.2**. Controlled wirelessly via **Sony PS3 DualShock 3** over raw Bluetooth L2CAP/HID.

---

## 🚀 Key Performance Architectures

1. **Dual-Core Asynchronous RTOS**:
   * **Core 0**: Exclusively manages the Bluetooth L2CAP stack and controller packet reception.
   * **Core 1**: Dedicated high-priority real-time Motion Task running at a deterministic **100 Hz (10 ms)** via `vTaskDelayUntil()`.
2. **20 kHz Ultrasonic Silent PWM**:
   * Completely eliminates audible motor whine.
   * Drastically reduces MOSFET switching heat and provides smooth low-speed torque for ball handling and delicate dribbling.
3. **Active Dynamic Electric Braking**:
   * Motor pins pull to ground on neutral input, capturing back-EMF and stopping the robot instantly on a dime without drifting.
4. **Maximized RF Penetration (+9 dBm)**:
   * Sets Bluetooth Classic transmission power to the hardware maximum (`ESP_PWR_LVL_P9`) for reliable control in crowded tournament environments.
5. **Integrated Solenoid Kicker (GPIO 5 & 15)**:
   * Built-in non-blocking hardware pulse generator (**50 ms safe auto-cutoff**) to prevent solenoid coil burnout.
6. **250ms Deadman Failsafe Watchdog**:
   * Automatic emergency motor cutoff if Bluetooth packets are interrupted for >250 ms.

---

## 📂 Repository Organization

```text
Soccer-Nopal/
├── main/                          # Native ESP-IDF application source
│   ├── CMakeLists.txt             # Component build definition
│   └── main.c                     # 100Hz RTOS motion loop & controller handlers
├── components/                    # Pure C driver components
│   └── ps3/                       # PS3 DualShock 3 Bluetooth L2CAP/HID driver
├── arduino/                       # Legacy & reconstructed Arduino sketches
│   ├── Soccer_Nopal_Roamer/       # Reverse-engineered firmware from original ROM
│   └── Soccer_Nopal_Optimized/    # Optimized Arduino reference sketch
├── web_interface/                 # Standalone Web Configuration GUI
│   └── index.html                 # Complete mobile-friendly configuration dashboard
├── firmware_dump/                 # Raw flash backup & extracted partition binaries
│   ├── esp32_firmware_backup.bin  # 4 MB bit-for-bit physical ROM dump
│   ├── config_dump.json           # Calibrated NVS speed & ramp parameters
│   ├── extracted_html_tokens.txt  # Tokens extracted from flash
│   └── partitions/                # Bootloader, partitions, nvs, otadata, app0
├── docs/                          # Hardware schematics & user documentation
│   ├── HARDWARE_PINOUT.md         # Full pin assignment & electrical guidelines
│   ├── CONTROLLER_MAPPING.md      # PS3 controller button & stick layout guide
│   └── FIRMWARE_ANALYSIS.md       # Complete binary extraction report
├── tools/                         # Helper scripts & CLI configs
│   ├── activate.ps1               # ESP-IDF PowerShell environment activation
│   ├── arduino-cli.yaml           # Arduino CLI configuration
│   └── eim_config.toml            # ESP-IDF tool manager config
├── CMakeLists.txt                 # ESP-IDF project root definition
├── sdkconfig.defaults             # Pre-configured Classic Bluetooth & PWM settings
├── partitions.csv                 # 4 MB flash partition table
└── .gitignore                     # Clean repository ignore list
```

---

## ⚡ Hardware Pinout

| Pin | Function | Description |
| :--- | :--- | :--- |
| **`GPIO 25`** | Right Motor PWM A (`MRA`) | Forward channel, 20 kHz LEDC PWM |
| **`GPIO 26`** | Right Motor PWM B (`MRB`) | Reverse channel, 20 kHz LEDC PWM |
| **`GPIO 33`** | Left Motor PWM A (`MLA`) | Forward channel, 20 kHz LEDC PWM |
| **`GPIO 32`** | Left Motor PWM B (`MLB`) | Reverse channel, 20 kHz LEDC PWM |
| **`GPIO 5`**  | Solenoid Kicker Primary | 50ms pulse for MOSFET driver |
| **`GPIO 15`** | Solenoid Kicker Auxiliary | Auxiliary ground switch / companion line |
| **`GPIO 0`**  | BOOT Button | Onboard button for Web AP mode |

*See [`docs/HARDWARE_PINOUT.md`](file:///d:/Arduino/Soccer%20Nopal/docs/HARDWARE_PINOUT.md) for full wiring schematics and power recommendations.*

---

## 🎮 Controller Layout (PS3 DualShock 3)

| Control | Input | Motor / Actuator Output |
| :--- | :--- | :--- |
| **Forward (Maju)** | D-Pad **UP** / **$\Delta$** / Stick **Up** | Both motors forward |
| **Backward (Mundur)** | D-Pad **DOWN** / **$\times$** / Stick **Down** | Both motors reverse |
| **Turn Left (Kiri)** | D-Pad **LEFT** / **$\square$** / Stick **Left** | Left reverse, Right forward (pivot) |
| **Turn Right (Kanan)**| D-Pad **RIGHT** / **$\bigcirc$** / Stick **Right** | Left forward, Right reverse (pivot) |
| **Speed: Fast** | Hold **`R1`** | Max velocity (`255` PWM) |
| **Speed: Medium** | Hold **`L1`** | Cruising velocity (`61` PWM) |
| **Speed: Low** | Default (No shoulder buttons) | Dribble velocity (`35` PWM) |
| **Shoot (Kick)** | Press **`R2`** or Click **`R3`** | 50ms Solenoid pulse (`GPIO 5`) |

*See [`docs/CONTROLLER_MAPPING.md`](file:///d:/Arduino/Soccer%20Nopal/docs/CONTROLLER_MAPPING.md) for full layout details.*

---

## 🛠️ Building & Flashing

### Prerequisites
* [Espressif ESP-IDF v5.3.2](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/get-started/)
* Connected ESP32 board on serial port (e.g., `COM8` on Windows or `/dev/ttyUSB0` on Linux)

### Build & Flash via ESP-IDF CLI
```powershell
# 1. Activate ESP-IDF environment
. C:\Espressif\tools\Microsoft.v5.3.2.PowerShell_profile.ps1

# 2. Build project
idf.py build

# 3. Flash to ESP32
idf.py -p COM8 flash

# 4. Monitor serial logs
idf.py -p COM8 monitor
```

---

## 📄 License
This project is open-source under the [MIT License](file:///d:/Arduino/Soccer%20Nopal/LICENSE).
