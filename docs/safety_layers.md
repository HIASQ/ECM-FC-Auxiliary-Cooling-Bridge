# Multi-Layer Safety Architecture

This document explains the **4 independent protection layers** implemented in the Smart-Fan-Guardian project. Each layer is designed to work autonomously — if any layer fails, the remaining layers still protect the engine from overheating.

---

## Overview
┌─────────────────────────────────────────────────────────────────────────────┐
│ ENGINE TEMPERATURE │
│ │ │
│ ▼ │
│ ┌─────────────────────────────────────────────────────────────────────┐ │
│ │ LAYER 1: ESP8266 + DS18B20 + Web Dashboard │ │
│ │ • Real-time temperature monitoring │ │
│ │ • Configurable timing cycles │ │
│ │ • Manual override via web interface │ │
│ │ • OTA updates for remote maintenance │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
│ │ │
│ ▼ (if sensor fails or temp exceeds) │
│ ┌─────────────────────────────────────────────────────────────────────┐ │
│ │ LAYER 2: Software Temperature Condition │ │
│ │ • Timer runs ONLY when temperature ≥ threshold │ │
│ │ • Below threshold: Fan runs continuously (safe state) │ │
│ │ • Prevents unnecessary cycling when engine is cool │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
│ │ │
│ ▼ (if ESP loses power or crashes) │
│ ┌─────────────────────────────────────────────────────────────────────┐ │
│ │ LAYER 3: Physical Wiring (Fail-ON Logic) │ │
│ │ • Relay configured as NC (Normally Closed) │ │
│ │ • If ESP loses power → NC contact keeps relay coil grounded │ │
│ │ • Result: Fan runs continuously — safe state for overheating │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
│ │ │
│ ▼ (if all electronics fail) │
│ ┌─────────────────────────────────────────────────────────────────────┐ │
│ │ LAYER 4: Hardware Thermal Switch (Independent Circuit) │ │
│ │ • 90°C NC thermal switch spliced into factory ECT sensor wire │ │
│ │ • When temp reaches 90°C → switch OPENS → ECU triggers fan │ │
│ │ • Works even if ESP is dead, power is lost, or software crashes │ │
│ │ • No code, no electronics — pure mechanical-electrical fail-safe │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
│ │ │
│ ▼ │
│ RADIATOR FAN RUNS │
└─────────────────────────────────────────────────────────────────────────────┘


---

## Layer 1: ESP8266 Intelligent Control

### Components
- ESP8266 (NodeMCU / Wemos D1 / ESP-01)
- DS18B20 temperature sensor
- 5V relay module
- Web dashboard interface

### How It Works
1. The DS18B20 reads temperature from the radiator hose (calibrated offset applied)
2. The ESP runs a timing cycle: Fan OFF for X minutes → Fan ON for Y minutes
3. Web interface allows:
   - Real-time temperature monitoring
   - Adjusting ON/OFF timing
   - Manual override (Force ON / Force OFF)
   - OTA firmware updates

### Code Logic (from ECM-FC-Auxiliary-Cooling-Bridge.ino)
```cpp
// Timer cycle logic
if (isOn && (currentMillis - previousMillis >= onTime)) {
    digitalWrite(RELAY_PIN, LOW); // Turn fan ON
    isOn = false;
} else if (!isOn && (currentMillis - previousMillis >= offTime)) {
    digitalWrite(RELAY_PIN, HIGH); // Turn fan OFF
    isOn = true;
}

Failure Modes Handled
Sensor failure: Temperature reads -99.9°C → web interface shows error

WiFi failure: ESP still runs timer locally (settings in EEPROM)

Power cycle: Settings persist via EEPROM

Layer 2: Software Temperature Condition
How It Works
The timer cycle only activates when the engine temperature reaches a user-defined threshold:

Condition	Behavior
Temp < Threshold	Fan runs continuously (safe state — prevents overheating)
Temp ≥ Threshold	Timer cycle activates (Fan OFF for X min / ON for Y min)
Why This Matters
Prevents unnecessary fan cycling when engine is cold

Extends relay and fan motor lifespan

Still ensures cooling when engine reaches operating temperature

Configuration
Set via web interface:

Temperature Condition: Active / Inactive

Target Temperature: 10°C - 100°C

Code Logic

if (isTempConditionActive) {
    if (temperature != -99.9 && temperature >= targetTemp) {
        // Temperature reached → enable timer cycle
        isAutoMode = true;
        shouldAutoTimerRun = true;
    } else {
        // Below threshold → fan runs continuously (safe state)
        digitalWrite(RELAY_PIN, LOW); // Fan ON
        isAutoMode = false;
    }
}

Layer 3: Physical Wiring (Fail-ON Logic)
The Genius of NC Relay Configuration
Most DIY projects use NO (Normally Open) relays. This project uses NC (Normally Closed) for fail-safe operation.

How It's Wired

┌─────────────────┐     ┌─────────────────┐
│   ESP Relay     │     │  Car Relay      │
│                 │     │                 │
│  ┌───────────┐  │     │  ┌───────────┐  │
│  │    COM    ├──┼─────┼──┤   Pin 86  │  │
│  │           │  │     │  │           │  │
│  │    NC     ├──┼─────┼──┤    GND    │  │
│  └───────────┘  │     │  └───────────┘  │
│                 │     │                 │
│  Active HIGH   │     │  Pin 85 → +12V  │
│  (HIGH = OFF)  │     │  (Ignition)     │
└─────────────────┘     └─────────────────┘

Normal Operation
ESP sends HIGH (fan OFF) → relay coil energized → NC contact OPENS

Car relay coil circuit is OPEN → fan follows ESP timer logic

Failure Scenario (ESP loses power)
Relay coil de-energizes → NC contact CLOSES

Car relay coil now has GND → car relay activates → fan runs continuously

Why This is Critical
ESP crash? Fan runs.

Power supply fails? Fan runs.

WiFi lost? Fan runs (ensures safe state)

Layer 4: Hardware Thermal Switch (Independent Circuit)
Overview
This is the ultimate backup layer — completely independent of all electronics. It uses the car's own ECU fail-safe logic.

How the ECU Fail-Safe Works
Most vehicles have a built-in safety feature: If the engine temperature sensor circuit is OPEN, the ECU activates the radiator fan.

This project exploits that feature.

The Wiring Modification

┌─────────────────┐     ┌─────────────────┐
│   Factory ECT   │     │      ECU        │
│     Sensor      │     │                 │
│                 │     │                 │
│  ┌───────────┐  │     │  ┌───────────┐  │
│  │  Signal   ├──┼─────┼──┤  Sensor   │  │
│  │           │  │     │  │  Input    │  │
│  └───────────┘  │     │  └───────────┘  │
│                 │     │                 │
│  ┌───────────┐  │     │  ┌───────────┐  │
│  │   GND     ├──┼─┐   │  │   GND     │  │
│  └───────────┘  │ │   │  └───────────┘  │
└─────────────────┘ │   └─────────────────┘
                    │
                    │   ┌─────────────────┐
                    └───┤ 90°C NC Thermal│
                        │     Switch      │
                        └─────────────────┘
                                │
                                ▼
                              GND

                              How It Works
Temperature	Switch State	ECU Sees	Fan Behavior
< 90°C	CLOSED	Normal sensor signal	ECU controls fan normally
≥ 90°C	OPENS	Open circuit	ECU triggers fan (fail-safe)
Why This is Genius
No electronics involved — just a mechanical thermal switch

Works even if all electronics fail — ESP dead, power lost, software crash

Uses the car's own fail-safe — you're not fighting the ECU, you're leveraging it

Redundant — completely independent from Layers 1-3

Summary: What Each Layer Protects Against
Failure Scenario	Layer 1	Layer 2	Layer 3	Layer 4
DS18B20 sensor fails	⚠️ Shows error	✅ Fan runs continuous	✅ Works	✅ Works
ESP crashes / hangs	❌ Fails	❌ Fails	✅ Fan runs	✅ Works
ESP loses power	❌ Fails	❌ Fails	✅ Fan runs	✅ Works
WiFi network down	⚠️ Local mode only	✅ Works	✅ Works	✅ Works
All electronics fail	❌ Fails	❌ Fails	❌ Fails	✅ Fan runs
Extreme overheating (90°C+)	✅ Can trigger	✅ Can trigger	✅ Works	✅ Guaranteed trigger
Installation Notes for Layer 4
Thermal Switch Selection
Use a 90°C Normally Closed (NC) thermal switch

Rated for automotive temperatures (usually -40°C to 150°C)

Choose one with appropriate current rating (at least 1A)

Wiring Instructions
Locate the factory engine coolant temperature (ECT) sensor

Identify the ground wire (not the signal wire — consult vehicle wiring diagram)

Cut this ground wire

Connect the thermal switch in series between the sensor ground and ECU ground

Mount the thermal switch to the engine block or thermostat housing using thermal compound

Testing
Use a heat gun or multimeter to verify the switch opens at 90°C

With engine cold, the switch should be closed (continuity)

When engine reaches 90°C, the switch should open → fan should activate regardless of other layers

Conclusion
This 4-layer architecture ensures your engine will never overheat, regardless of what fails. Each layer is designed to be independent and fail-safe, making this system suitable for real-world automotive use where reliability is critical.

The system has been tested in a real vehicle for over 4 months with zero failures.

Remember: Good engineering isn't about making things work when everything is perfect — it's about making things work when everything fails.



