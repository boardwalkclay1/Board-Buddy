# Board Buddy — ESP8266 + mmWave Radar Wiring Diagram

This file documents the correct wiring for the Board Buddy ESP8266 build using a UART‑based mmWave radar sensor (LD2410 / HLK‑LD2410B style).

---

## 📡 mmWave Radar → ESP8266 Wiring

| Radar Pin | ESP8266 Pin | Notes |
|----------|-------------|-------|
| **VCC** | **3.3V** | Radar must run on 3.3V only |
| **GND** | **GND** | Common ground required |
| **TX** | **RX (GPIO3 / D9)** | Radar → ESP8266 data |
| **RX** | **TX (GPIO1 / D10)** | Optional (only needed for radar config) |
| **OUT / PWM** | *Not used* | Board Buddy uses UART mode only |

---

## 🧠 ESP8266 Pin Reference (NodeMCU / Wemos D1 Mini)

| Label | GPIO | Function |
|-------|-------|----------|
| **D9** | GPIO3 | Hardware RX (connect radar TX) |
| **D10** | GPIO1 | Hardware TX (optional) |
| **3V3** | 3.3V | Power for radar |
| **GND** | GND | Ground |

---

## ⚡ Power Notes

- Radar modules **must** be powered from **3.3V**, not 5V.  
- ESP8266 3.3V pin can supply enough current for LD2410‑class sensors.  
- Always share **GND** between radar and ESP8266.

---

## 🖼️ Wiring Diagram (Visual)

