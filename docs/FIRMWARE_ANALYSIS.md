# ESP32 Firmware Analysis & Extracted Files

**Device**: ESP32-D0WD-V3 (Revision 3.1, 4MB Flash)  
**Port**: `COM8` (`USB-SERIAL CH340`)  
**MAC Address**: `b4:bf:e9:0d:91:10`  
**Original Framework**: Arduino-ESP32 (Core v2.0.17 on ESP-IDF v4.4.7)  
**Project Identified**: **Roamer N' Destroyer - Roamer Mode** (Soccer Robot)

---

## 1. Directory File List

| File | Type | Description |
| :--- | :--- | :--- |
| [`Soccer_Nopal_Roamer.ino`](file:///d:/Arduino/Soccer%20Nopal/Soccer_Nopal_Roamer.ino) | C++ / Arduino | Fully reconstructed and formatted Arduino sketch matching the chip's firmware logic. |
| [`web_interface.html`](file:///d:/Arduino/Soccer%20Nopal/web_interface.html) | HTML5 / CSS / JS | Standalone Web GUI extracted from the ESP32 firmware (can be opened in any browser). |
| [`config_dump.json`](file:///d:/Arduino/Soccer%20Nopal/config_dump.json) | JSON | Complete set of configuration keys and calibrated speed parameters from NVS. |
| [`esp32_firmware_backup.bin`](file:///d:/Arduino/Soccer%20Nopal/esp32_firmware_backup.bin) | Binary (4 MB) | Full bit-by-bit physical flash memory dump read from `0x00000000` to `0x00400000`. |
| [`activate.ps1`](file:///d:/Arduino/Soccer%20Nopal/activate.ps1) | PowerShell | One-command activation script for ESP-IDF v5.3.2 tools. |
| `extracted_partitions/` | Folder | Individual binary dumps carved by partition table (see Section 2). |

---

## 2. Partition Table Map

The partition table was decoded from physical address `0x00008000`:

| Name | Type | Subtype | Offset | Size | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **bootloader** | System | Bootloader | `0x00001000` | 28 KB | Extracted (`bootloader.bin`) |
| **partitions** | System | Partitions | `0x00008000` | 4 KB | Extracted (`partitions.bin`) |
| **nvs** | Data | `0x02` (NVS) | `0x00009000` | 20 KB | Extracted (`nvs.bin`) — Contains configs & credentials |
| **otadata** | Data | `0x00` (OTA) | `0x0000E000` | 8 KB | Extracted (`otadata.bin`) |
| **app0** | App | `0x10` (Factory) | `0x00010000` | 3,072 KB | Extracted (`app0.bin`) — Main executable binary |
| **spiffs** | Data | `0x82` (SPIFFS) | `0x00310000` | 896 KB | Unformatted / empty (all `0xFF`) |
| **coredump** | Data | `0x03` (Coredump)| `0x003F0000` | 64 KB | Empty (all `0xFF`) |

---

## 3. Extracted Hardware & Network Configuration

### Motor Driver Pins
- **Left Motor Pin A (`pin_mla`)**: `GPIO 33` (LEDC PWM)
- **Left Motor Pin B (`pin_mlb`)**: `GPIO 32` (LEDC PWM)
- **Right Motor Pin A (`pin_mra`)**: `GPIO 25` (LEDC PWM)
- **Right Motor Pin B (`pin_mrb`)**: `GPIO 26` (LEDC PWM)

### Bluetooth (PS3 Controller)
- **Paired PS3 MAC Address**: `22:3d:75:2a:f0:b2`
- **Protocol**: Sony DualShock 3 via L2CAP Bluetooth Host

### Wi-Fi Configuration AP
- **SSID**: `RND OS [R' Mode]`
- **Password**: `M3raky4T`
- **Toggle Method**: Press onboard **BOOT** button (`GPIO 0`) **3 times**
- **Safety Feature**: Web Mode AP automatically turns off whenever the PS3 controller connects.

---

## 4. Calibrated Motion Parameters (0–255)

```json
{
  "ramp_up": 200,       // Acceleration time (ms)
  "ramp_down": 250,     // Deceleration time (ms - anti-flip)
  "forward": {
    "F_FAST_L": 255,   "F_FAST_R": 255,
    "F_MEDIUM_L": 61,  "F_MEDIUM_R": 61,
    "F_LOW_L": 20,     "F_LOW_R": 20
  },
  "backward": {
    "B_FAST_L": 171,   "B_FAST_R": 171,
    "B_MEDIUM_L": 61,  "B_MEDIUM_R": 61,
    "B_LOW_L": 20,     "B_LOW_R": 20
  },
  "turn_rates": {
    "TR_LOW_LR": 35,
    "TR_MEDIUM_LR": 65,
    "TR_FAST_LR": 100
  },
  "curve_movement": {
    "CRV_LOW_L": 255,   "CRV_LOW_R": 32,
    "CRV_MEDIUM_L": 0,  "CRV_MEDIUM_R": 0,
    "CRV_FAST_L": 255,  "CRV_FAST_R": 32
  },
  "s_speed": {
    "DEFAULT_LR": 0
  }
}
```
