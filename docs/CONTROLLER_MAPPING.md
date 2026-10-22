# PS3 DualShock 3 Controller Mapping Guide

**Project**: Soccer Nopal (Roamer N' Destroyer)  
**Host Protocol**: Sony DualShock 3 Bluetooth L2CAP / HID  
**Target MAC Address**: `22:3d:75:2a:f0:b2` (Configurable via NVS)  

---

## 1. Controller Layout Overview

```text
       [L1] Medium Speed                     [R1] Fast Speed
       [L2] Medium Speed                     [R2] Solenoid Kicker (Shoot)
            _=====_                               _=====_
           / _____ \                             / _____ \
          +.-'_____'-.-------------------------.-'_____'-.+
         /   |     |  '.        [SONY]        .'  |  _  |   \
        / ___| /^\ |___ \                    / ___| (^) |___ \
       | |<   (+)   >| |      [SELECT] [START] | |(_)   (O)| |
       | |___ \v/ ___| |       [PS BUTTON]     | |___ (X) ___| |
       |\    |     |  /  /---\             /---\  \  |     |  /|
       | \   `-----' /  |  LY |           |  RY |  \ `-----' / |
       |  '-._____.-'    \___/             \___/    '-._____.-'  |
       |                [LX / LY]         [RX / RY]              |
```

---

## 2. Motion & Steering Controls

The robot supports simultaneous input from **D-Pad**, **PlayStation Face Buttons**, and **Dual Analog Sticks**:

| Motion | D-Pad Button | Face Button | Analog Stick Input | Motor Behavior |
| :--- | :--- | :--- | :--- | :--- |
| **Maju (Forward)** | `D-Pad UP` | `Triangle` ($\Delta$) | Stick Pushed Up (`Y < -40`) | Both wheels spin forward ($+V_L, +V_R$) |
| **Mundur (Backward)** | `D-Pad DOWN` | `Cross` ($\times$) | Stick Pushed Down (`Y > 40`) | Both wheels spin reverse ($-V_L, -V_R$) |
| **Belok Kiri (Pivot Left)** | `D-Pad LEFT` | `Square` ($\square$) | Stick Pushed Left (`X < -40`) | Left reverse, Right forward ($-V_{TR}, +V_{TR}$) |
| **Belok Kanan (Pivot Right)**| `D-Pad RIGHT`| `Circle` ($\bigcirc$) | Stick Pushed Right (`X > 40`) | Left forward, Right reverse ($+V_{TR}, -V_{TR}$) |
| **Stop / Neutral** | Released | Released | Deadzone ($-40 \le X, Y \le 40$) | Active Dynamic Electric Brake ($0, 0$) |

> **Dual Analog Support**: Both the **Left Stick (LX/LY)** and the **Right Stick (RX/RY)** are monitored simultaneously. Whichever stick has the larger deflection takes priority.

---

## 3. Speed Modifiers (Gear System)

| Speed Tier | Trigger Buttons | Left Speed | Right Speed | Turn Rate | Use Case |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Fast (Turbo)** | Hold **`R1`** | `255` (100%) | `255` (100%) | `115` | Fast sprints across arena |
| **Medium (Cruising)** | Hold **`L1`** | `61` (24%) | `61` (24%) | `75` | Controlled open-field maneuvering |
| **Low (Dribble)** | *Default* (No shoulder buttons) | `35` (14%) | `35` (14%) | `45` | Precision ball dribbling & alignment |

---

## 4. Special Actions & Weapons

| Action | Controller Trigger | Hardware Actuator | Description |
| :--- | :--- | :--- | :--- |
| **Tendang Bola (Kick)** | Press **`R2`** or Click **`R3`** | `GPIO 5` & `GPIO 15` | Fires solenoid with a **50 ms** non-blocking pulse |
| **Failsafe Watchdog** | Disconnect or timeout | All Motors & Solenoid | Cuts all motor PWM to 0 within 250ms of packet loss |
