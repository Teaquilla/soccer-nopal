# Hardware Pinout & Wiring Specifications

**Project**: Soccer Nopal (Roamer N' Destroyer)  
**Microcontroller**: ESP32-D0WD-V3 (Dual-Core Xtensa LX6 @ 240MHz, 4MB Flash)  
**Primary Framework**: Native Espressif ESP-IDF v5.3.2  

---

## 1. ESP32 Pin Assignment Map

| Function | ESP32 GPIO | Pin Type | Connected Hardware | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Right Motor Forward (MRA)** | `GPIO 25` | LEDC PWM (CH 0) | Motor Driver IN1 / A | 20 kHz Ultrasonic PWM (0–255) |
| **Right Motor Reverse (MRB)** | `GPIO 26` | LEDC PWM (CH 1) | Motor Driver IN2 / B | 20 kHz Ultrasonic PWM (0–255) |
| **Left Motor Forward (MLA)** | `GPIO 33` | LEDC PWM (CH 2) | Motor Driver IN3 / A | 20 kHz Ultrasonic PWM (0–255) |
| **Left Motor Reverse (MLB)** | `GPIO 32` | LEDC PWM (CH 3) | Motor Driver IN4 / B | 20 kHz Ultrasonic PWM (0–255) |
| **Solenoid Kicker Primary** | `GPIO 5` | GPIO Output | MOSFET Gate / Relay IN | 50ms pulse non-blocking safety cutoff |
| **Solenoid Kicker Auxiliary** | `GPIO 15` | GPIO Output | Aux Driver / Ground Switch | 50ms pulse synchronized with GPIO 5 |
| **Onboard Status LED** | `GPIO 2` | GPIO Output | Onboard Blue LED | Connection & heartbeat status |
| **BOOT Mode Button** | `GPIO 0` | GPIO Input | Onboard BOOT Button | Pull-up enabled; used for AP toggle |

---

## 2. Visual Pinout Reference

```text
                             [ ANTENNA PCB ]
                      +---------------------------+
               3V3   -|                           |-  VIN (5.0V Input from BEC/Buck)
               GND   -|                           |-  GND (Common Ground)
 (Aux Solenoid) D15  -|                           |-  D13
                D2   -|                           |-  D12
                D4   -|                           |-  D14
               RX2   -|                           |-  D27
               TX2   -|                           |-  D26 (Right Motor PWM B - MRB)
 (Solenoid) ->  D5   -|                           |-  D25 (Right Motor PWM A - MRA)
               D18   -|                           |-  D33 (Left Motor PWM A - MLA)
               D19   -|                           |-  D32 (Left Motor PWM B - MLB)
               D21   -|                           |-  D35 (Input Only)
               RX0   -|                           |-  D34 (Input Only)
               TX0   -|                           |-  VN
               D22   -|                           |-  VP
               D23   -|                           |-  EN (Reset Button)
                      +---------------------------+
                               [ USB PORT ]
```

---

## 3. Electrical & Power Guidelines

### Motor Driver (H-Bridge)
* Supported drivers: Dual H-Bridge (e.g., BTS7960, TB6612FNG, L298N, or discrete MOSFET H-Bridge).
* Frequency: Configured to **20,000 Hz (20 kHz)** to eliminate high-pitched acoustic motor whine and minimize MOSFET heating during low-speed PWM dribbling.
* Active Dynamic Braking: Pulling both IN1 and IN2 to LOW grounds back-EMF, providing instant stops without coasting.

### Solenoid Kicker Circuit
* **Operating Voltage**: 12V – 24V DC.
* **Driver**: High-current N-channel MOSFET (e.g., IRF3205 / IRLZ44N / D4184 module) with gate resistor (100–220 $\Omega$) driven by `GPIO 5`.
* **Flyback Diode**: A fast recovery or standard 1N4007 / 1N5819 diode MUST be placed across the solenoid coil terminals in reverse polarity to clamp back-EMF inductive spikes.
* **Failsafe**: Firmware enforces a maximum **50 ms** pulse duration to prevent coil burnout.
