# ESP8266 Cloud-Connected Thermostat

A DIY smart thermostat built with a Wemos D1 Mini (ESP8266). It is controllable via the Arduino Cloud, features a local 8-digit LED display for at-a-glance information, and includes critical safety features for controlling appliances like refrigerators.



## Features

* **Remote Control:** Monitor and control all settings from anywhere using the Arduino IoT Cloud dashboard.
* **Local Display:** An 8-digit, 7-segment LED display shows the current operating mode, setpoint, and live temperature.
* **Heating & Cooling Modes:** Can be configured to control either a heating element (like a resistor) or a cooling appliance (like a refrigerator or fan).
* **Compressor Protection:** Includes an adjustable time delay to prevent a cooling appliance's compressor from turning on and off too quickly (short-cycling), which can cause damage.
* **Offline Reliability:** All settings are saved to the device's internal EEPROM. If the internet connection is lost, the thermostat will continue to operate flawlessly with its last known settings.
* **Adjustable Parameters:** Setpoint, hysteresis, mode, and protection time can all be changed on the fly from the cloud dashboard.

---
## Understanding the LED Display

The 8-digit 7-segment display provides a real-time dashboard of the thermostat's status. The layout is as follows:

**[ M. | SSS |   | TTT ]**



Here is a breakdown of each section, from left to right:

* **Character 1: Mode & Protection (M.)**
    * The letter indicates the operating mode:
        * **H** = **H**eating Mode
        * **C** = **C**ooling Mode
    * The decimal point (`.`) indicates the **compressor protection** status:
        * **Decimal Point ON:** Protection is **active**. The relay is locked and will not change state.
        * **Decimal Point OFF:** Protection is **inactive**. It is safe for the relay to change state.

* **Characters 2-4: Setpoint (SSS)**
    * This 3-digit block shows the target temperature (**S**etpoint). It uses a compact format to display positive, negative, and decimal values.

* **Character 5: Separator ( )**
    * This is always a blank space to visually separate the setpoint and the current temperature.

* **Characters 6-8: Current Temperature (TTT)**
    * This 3-digit block shows the live **T**emperature reading from the sensor.

---
## Hardware Required

* Wemos D1 Mini (or any similar ESP8266 board)
* DS18B20 Temperature Sensor
* MAX7219 8-digit 7-segment LED display module
* 5V Relay Module (this project is coded for a **Normally Closed** relay)
* **4.7kΩ Resistor** (for the DS18B20 pull-up)
* **0.1µF (100nF) Ceramic Capacitor** (optional but highly recommended for sensor stability)
* Jumper wires and a breadboard or perfboard
* A separate 5V power supply for the relay module

---
## Wiring

Correct wiring is crucial for stability, especially for the sensor and relay.



| Wemos D1 Mini | Component          | Notes                                     |
| :------------ | :----------------- | :---------------------------------------- |
| **GND** | GND on DS18B20     |                                           |
| **3V3** | VCC on DS18B20     |                                           |
| **D1** | Data on DS18B20    | Connect 4.7kΩ resistor between D1 and 3V3 |
| **GND** | GND on MAX7219     |                                           |
| **5V** | VCC on MAX7219     |                                           |
| **D6** | DIN on MAX7219     |                                           |
| **D7** | CS on MAX7219      |                                           |
| **D8** | CLK on MAX7219     |                                           |
| **D4** | IN on Relay Module |                                           |

**Relay Power:**
The relay module must be powered by a separate 5V power supply.
* Relay **VCC** -> External 5V **(+)**
* Relay **GND** -> External 5V **(-)**
* **CRITICAL:** Connect the External 5V **GND** to a **GND** pin on the Wemos D1 Mini to create a common ground.

---
## Software & Setup

This project uses the local Arduino IDE for compilation to ensure the correct library versions are used.

### 1. Arduino Cloud Setup

1.  Go to **[cloud.arduino.com](https://cloud.arduino.com)**.
2.  Create a new **Thing**.
3.  Associate your ESP8266 device, saving the **Device ID** and **Secret Key**.
4.  Create exactly these 5 **Cloud Variables**:

| Variable Name     | Type                    | Permission    |
| ----------------- | ----------------------- | ------------- |
| `currentTemp`     | `Floating Point Number` | Read Only     |
| `hysteresis`      | `Floating Point Number` | Read & Write  |
| `mode`            | `Boolean`               | Read & Write  |
| `protectionTime`  | `Integer Number`        | Read & Write  |
| `setpoint`        | `Floating Point Number` | Read & Write  |

5.  Configure your Wi-Fi credentials in the "Network" tab.
6.  Create a **Dashboard** and link widgets (e.g., Gauges, Switches, Steppers) to your cloud variables.

### 2. Local Arduino IDE Setup

1.  Install the [Arduino IDE](https://www.arduino.cc/en/software).
2.  Add the ESP8266 board manager URL in **File > Preferences**. Use: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3.  Go to **Tools > Board > Boards Manager...** and install the `esp8266` package.
4.  Go to **Sketch > Include Library > Manage Libraries...** and install the following:
    * `ArduinoIoTCloud`
    * `OneWireNg`
    * `DallasTemperature`
    * `LedControl`
5.  Create a new sketch and create three tabs:
    * **Main Sketch File (`.ino`)**: Paste the main project code here.
    * **`thingProperties.h`**: Copy the code from the "Sketch" tab of your Arduino Cloud Thing into this file.
    * **`Secret`**: Create this tab and define your credentials like this:
        ```cpp
        #define SECRET_SSID "YourNetworkName"
        #define SECRET_OPTIONAL_PASS "YourWiFiPassword"
        #define SECRET_DEVICE_KEY "YourSecretDeviceKeyFromArduinoCloud"
        ```
6.  Select your board (e.g., "LOLIN(WEMOS) D1 R2 & mini") and the correct COM port.
7.  Upload the sketch.

---
## How It Works

* **Thermostat Logic:** The core logic runs every 5 seconds. It compares the `currentTemp` to the `setpoint`. Hysteresis creates a "dead zone" (`setpoint ± hysteresis`) to prevent the relay from switching rapidly.
    * In **Cool Mode**, the relay turns ON if the temperature rises above the upper limit.
    * In **Hot Mode**, the relay turns ON if the temperature drops below the lower limit.
* **Compressor Protection:** In Cool Mode, whenever the relay changes state, a timer starts. The relay is "locked" and cannot be changed again until the `protectionTime` (in seconds) has elapsed. This is indicated by a decimal point on the display's first character.
* **Cloud Sync:** The device syncs its `currentTemp` to the cloud. When you change a setting on the dashboard, a callback function is triggered on the device, which updates the local variable and saves the new value to the EEPROM.
* **Display:** The local display is updated every 5 seconds, providing a real-time dashboard of the thermostat's status.

---
## License

This project is licensed under the MIT License.
