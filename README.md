# Precision Syringe Pump System (ESP32 + TMC2209)

![Project Status](https://img.shields.io/badge/Status-Prototype%20Validated-success)
![Platform](https://img.shields.io/badge/Platform-ESP32-blue)
![License](https://img.shields.io/badge/License-MIT-green)

## 📖 Overview
This project is a medical-grade automated syringe pump designed for high-precision fluid delivery in laboratory and clinical settings. [cite_start]Developed as part of the **Medical Design Course** at the **School of Biomedical Engineering, International University (VNU-HCM)** [cite: 1-4][cite_start], the system focuses on resolving common intravenous administration errors through automation and safety monitoring[cite: 76, 89].

[cite_start]The device utilizes an **ESP32 microcontroller** and **TMC2209 SilentStepStick driver** to achieve a verified dosing accuracy of **99.62%**[cite: 265]. It features a custom 3D-printed enclosure, a real-time TFT display interface, and a closed-loop safety system.

![Device Design](device-design.png)
[cite_start]*(Figure 9: The complete design of Syringe pump machine [cite: 184])*

## ✨ Key Features
* [cite_start]**High Precision:** Volumetric accuracy of **99.62%** with a standard deviation of **±0.012 mL** (verified via gravimetric testing)[cite: 265].
* [cite_start]**Wide Flow Range:** Capable of flow rates from **0.1 mL/h to 1500 mL/h**[cite: 139].
* [cite_start]**Silent Operation:** Utilizes TMC2209 drivers with **StealthChop** technology for vibration-free fluid delivery[cite: 369].
* **Advanced Safety:**
    * [cite_start]**Soft Start Algorithm:** Prevents motor stalling by gradually ramping velocity during startup[cite: 213].
    * [cite_start]**RPM Limiting:** Firmware Hard Limit prevents speeds exceeding 50 RPM to protect mechanics[cite: 208].
    * [cite_start]**Hardware Interlocks:** Dual limit switches and clamp sensors to detect mechanical faults[cite: 252].
* [cite_start]**User Interface:** 1.69-inch IPS TFT display for real-time monitoring and parameter adjustment[cite: 254].

## 🛠 System Architecture

### Hardware Components (BOM)
[cite_start]The system is built around the following key components[cite: 146, 150]:
| Component | Specification | Role |
| :--- | :--- | :--- |
| **MCU** | ESP32-WROOM-32 | Central processing and logic control |
| **Driver** | TMC2209 | UART-controlled stepper driver (SilentStepStick) |
| **Motor** | NEMA 17 Stepper | 1.8° step angle, 200 steps/rev |
| **Transmission** | T8 Lead Screw | 2mm pitch, direct coupling (1:1 ratio) |
| **Display** | 1.69" ST7789 TFT | User interface and status display |
| **Power** | MP2482 Buck Converter | Steps down 12V to 5V for logic |
| **Safety** | TPS3823-33 | Watchdog timer for system reliability |

### Mechanical Design
The chassis is 3D printed and features a **rigid linear guide system**:
* [cite_start]**Structure:** Stainless steel guide rods and LM5UU linear bearings ensure alignment[cite: 150, 249].
* [cite_start]**Mechanism:** Direct drive via T8 lead screw converts rotational motion to linear displacement[cite: 248].
* [cite_start]**Enclosure:** Includes a syringe clamp, transparent trap door, and integrated cooling fan for the driver [cite: 182-183].

## 💻 Firmware & Pinout

The firmware is written in C++ (Arduino Framework) and utilizes a **Finite State Machine (FSM)** to manage operations safely.

### Wiring Configuration
[cite_start]Based on `Main.ino` and PCB Schematics [cite: 344-346, 173]:

| ESP32 GPIO | Function | Description |
| :--- | :--- | :--- |
| **16 (RX) / 17 (TX)** | UART | Communication with TMC2209 Driver |
| **5 (CS) / 2 (DC)** | SPI | ST7789 TFT Display Control |
| **21** | Limit Switch | Driver Max Position (Extended) |
| **14** | Limit Switch | Driver Min Position (Retracted) |
| **19** | Sensor | Syringe Clamp/Trap Detection |
| **15** | Output | Buzzer for Audible Alerts |
| **25** | Output | NeoPixel RGB LED Status |
| **36** | Input | Start/Stop Button |
| **35, 33, 34, 32** | Input | Navigation Buttons (Right, Left, Up, Down) |

### Logic Flow
1.  [cite_start]**Homing Sequence:** Upon boot, the system executes `runHomingSequence()`, utilizing a custom acceleration ramp to retract the pusher block safely [cite: 370-380].
2.  **Infusion Calculation:** `startInfusion()` calculates the required motor RPM based on syringe diameter and target flow rate. [cite_start]It aborts if RPM > `MAX_RPM_LIMIT` [cite: 429-432].
3.  [cite_start]**Operation:** The system enters `STATE_PUMPING`, continuously monitoring `updateMotorLogic()` for limit switch triggers or completion events[cite: 435].

## 📊 Validation & Results

[cite_start]The system underwent rigorous testing using a gravimetric method (distilled water density ≈ 1.00 g/mL)[cite: 218].

* **Test Protocol:** 95 consecutive cycles of 5 mL delivery.
* **Results:**
    * **Mean Volume:** 4.981 mL
    * **Accuracy:** 99.62% (0.38% under-delivery)
    * **Precision:** Standard Deviation ±0.012 mL

![Test Results](test-results.png)
[cite_start]*(Figure 12: Consistency results over 95 continuous pumping cycles [cite: 267])*

## 🚀 Installation & Usage

1.  [cite_start]**Dependencies:** Install the following libraries in Arduino IDE or PlatformIO[cite: 344]:
    * `Adafruit_GFX`
    * `Adafruit_ST7789`
    * `TMCStepper`
    * `Adafruit_NeoPixel`
2.  **Configuration:**
    * Connect the hardware according to the pinout table.
    * Upload `Main.ino` to the ESP32.
3.  **Operation:**
    * Power on the device (12V DC).
    * Wait for the "Auto Loading" homing sequence to complete.
    * Use the **Settings** menu to define Syringe Diameter and Flow Rate.
    * Select **Start Run**.

## ⚠️ Known Limitations
* [cite_start]**One-Way Operation:** Current mechanical design supports infusion (pushing) only, not withdrawal[cite: 309].
* [cite_start]**LED Logic:** In current firmware version v1.0, the LED color logic may be inverted (Green for Stop, Yellow for Run) compared to standard safety protocols[cite: 302].

## 👥 Developer
* **Nguyen Tan Tri**


## 📄 License
This project is open-source for educational and research purposes.

---
[cite_start]*Based on the "Medical Design Course" Final Report, submitted Dec 2025.* [cite: 8]
