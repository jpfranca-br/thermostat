/**
 * ESP8266-based Smart Thermostat
 * * Controls a relay for heating/cooling based on a DS18B20 temperature sensor.
 * Integrates with Arduino IoT Cloud for remote monitoring and control.
 * Features a local 8-digit 7-segment display and saves settings to EEPROM.
 */

#include "thingProperties.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LedControl.h>
#include <EEPROM.h>

// --- Hardware Pin Definitions ---
#define ONE_WIRE_BUS D1       // DS18B20 temperature sensor
#define RELAY_PIN D4          // Relay control pin
const int DIN_PIN = D6;       // MAX7219 Data In
const int CS_PIN  = D7;       // MAX7219 Chip Select
const int CLK_PIN = D8;       // MAX7219 Clock
LedControl lc = LedControl(DIN_PIN, CLK_PIN, CS_PIN, 1);

// --- Global State Variables ---
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Local copies of thermostat settings (synced with EEPROM and Cloud)
float localSetpoint;
bool localMode;
float localHysteresis;
int localProtectionTime;

// Runtime state variables
bool protection;              // Is the compressor protection timer currently active?
bool relayState = false;      // Current state of the relay (true = ON, false = OFF)
unsigned long lastRelayChangeTime = 0; // Timestamp of the last relay state change
bool lastProtectionState;     // Used to detect when the protection status flips

// --- Timing Control ---
unsigned long previousMillis = 0; // Used for the main sensor reading timer
const long interval = 5000;       // Interval between sensor reads (in milliseconds)

// --- EEPROM Configuration ---
const byte MAGIC_NUMBER = 125;    // A unique number to check if EEPROM has been initialized


void setup() {
  // Initialize serial communication for debugging
  Serial.begin(115200);
  delay(1500); // Wait for serial monitor to connect

  // Configure hardware pins
  pinMode(RELAY_PIN, OUTPUT);
  // For a Normally Closed (NC) relay, HIGH energizes the coil, turning the appliance OFF.
  // This ensures a safe, off state on startup.
  digitalWrite(RELAY_PIN, HIGH);

  // Load persistent settings from EEPROM
  EEPROM.begin(16);
  loadSettingsFromEEPROM();

  // Initialize runtime state variables
  protection = true;          // Start in a safe (protected) state
  lastProtectionState = protection; // Sync initial state to prevent a false "changed" message

  // Print loaded settings to the terminal
  printSettings();

  // Connect to Wi-Fi and Arduino Cloud
  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  
  // Sync local settings (from EEPROM) up to the cloud dashboard
  syncLocalToCloud();

  // Set cloud connection verbosity for debugging
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  // Initialize the 7-segment display
  lc.shutdown(0, false);
  lc.setIntensity(0, 8);
  lc.clearDisplay(0);
  
  // Initialize the temperature sensor
  sensors.begin();
  // Set sensor resolution. 9-bit is fast (~94ms), 12-bit is high precision (~750ms).
  // A faster reading is better for stability when Wi-Fi is active.
  sensors.setResolution(12);
}

void loop() {
  // These functions must run on every loop cycle to maintain the cloud
  // connection and check the compressor protection timer.
  ArduinoCloud.update();
  checkProtection();

  // This block runs on a timed interval to read the sensor and execute thermostat logic.
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    printTimestamp(); Serial.print("Requesting temperature...");
    sensors.requestTemperatures(); 
    float tempC = sensors.getTempCByIndex(0);
    printTimestamp(); Serial.print(" Reading: ");

    if (tempC != DEVICE_DISCONNECTED_C) {
      Serial.print(tempC); Serial.println(" C");

      // Update the cloud variable and local display
      currentTemp = tempC;
      updateDisplay(tempC);
      
      // Run the main logic to decide if the relay should be on or off
      runThermostatLogic(tempC);
    } else {
      Serial.println(" FAILED!");
      displayError();
    }
  }
  
  // A small delay to yield processing time to the ESP8266's background Wi-Fi tasks.
  delay(10);
}

// =====================================================================
// FUNCTION DEFINITIONS
// =====================================================================

/**
 * @brief (Callback) Triggered when 'setpoint' is changed from the cloud dashboard.
 */
void onSetpointChange() { printTimestamp(); Serial.println(">>> Setpoint changed from Cloud Dashboard!"); setLocalSetpoint(setpoint); }

/**
 * @brief (Callback) Triggered when 'mode' is changed from the cloud dashboard.
 */
void onModeChange() { printTimestamp(); Serial.println(">>> Mode changed from Cloud Dashboard!"); setLocalMode(mode); }

/**
 * @brief (Callback) Triggered when 'hysteresis' is changed from the cloud dashboard.
 */
void onHysteresisChange() { printTimestamp(); Serial.println(">>> Hysteresis changed from Cloud Dashboard!"); setLocalHysteresis(hysteresis); }

/**
 * @brief (Callback) Triggered when 'protectionTime' is changed from the cloud dashboard.
 */
void onProtectionTimeChange() { printTimestamp(); Serial.println(">>> Protection Time changed from Cloud Dashboard!"); setLocalProtectionTime(protectionTime); }

/**
 * @brief Prints a formatted timestamp to the serial monitor for logging.
 */
void printTimestamp() { char timestamp[13]; sprintf(timestamp, "[%9lu] ", millis()); Serial.print(timestamp); }

/**
 * @brief Loads all settings from EEPROM into local variables on startup.
 * If EEPROM is uninitialized, it writes and uses default values.
 */
void loadSettingsFromEEPROM() { if (EEPROM.read(0) == MAGIC_NUMBER) { EEPROM.get(1, localSetpoint); localMode = EEPROM.read(5); EEPROM.get(6, localHysteresis); EEPROM.get(10, localProtectionTime); } else { printTimestamp(); Serial.println("First run: Initializing EEPROM with default settings."); localSetpoint = 4.0; localMode = false; localHysteresis = 1.0; localProtectionTime = 30; EEPROM.write(0, MAGIC_NUMBER); EEPROM.put(1, localSetpoint); EEPROM.write(5, localMode); EEPROM.put(6, localHysteresis); EEPROM.put(10, localProtectionTime); EEPROM.commit(); } }

/**
 * @brief Prints all current thermostat settings to the serial monitor.
 */
void printSettings() { printTimestamp(); Serial.println("--- Current Settings ---"); printTimestamp(); Serial.print("--> Setpoint: "); Serial.println(localSetpoint); printTimestamp(); Serial.print("--> Mode: "); Serial.println(localMode ? "Hot" : "Cool"); printTimestamp(); Serial.print("--> Hysteresis: "); Serial.println(localHysteresis); printTimestamp(); Serial.print("--> Protection Time: "); Serial.println(localProtectionTime); printTimestamp(); Serial.print("--> Protection state: "); Serial.println(protection ? "Active" : "Inactive"); printTimestamp(); Serial.println("------------------------------------"); }

/**
 * @brief Syncs the local variables (loaded from EEPROM) up to the cloud variables on startup.
 */
void syncLocalToCloud() { setpoint = localSetpoint; mode = localMode; hysteresis = localHysteresis; protectionTime = localProtectionTime; }

/**
 * @brief Updates the local 'setpoint' variable and saves it to EEPROM.
 */
void setLocalSetpoint(float val) { localSetpoint = val; EEPROM.put(1, localSetpoint); EEPROM.commit(); printSettings(); }

/**
 * @brief Updates the local 'mode' variable and saves it to EEPROM.
 */
void setLocalMode(bool val) { localMode = val; EEPROM.write(5, localMode); EEPROM.commit(); printSettings(); }

/**
 * @brief Updates the local 'hysteresis' variable and saves it to EEPROM.
 */
void setLocalHysteresis(float val) { localHysteresis = val; EEPROM.put(6, localHysteresis); EEPROM.commit(); printSettings(); }

/**
 * @brief Updates the local 'protectionTime' variable (0-999) and saves it to EEPROM.
 */
void setLocalProtectionTime(int val) { val = constrain(val, 0, 999); localProtectionTime = val; EEPROM.put(10, localProtectionTime); EEPROM.commit(); printSettings(); }

/**
 * @brief The core thermostat logic. Decides whether the relay should be ON or OFF.
 */
void runThermostatLogic(float tempC) { float lowerLimit = localSetpoint - localHysteresis; float upperLimit = localSetpoint + localHysteresis; if (localMode == true) { if (tempC < lowerLimit) setRelayState(true); else if (tempC > upperLimit) setRelayState(false); } else { if (protection == false) { if (tempC > upperLimit) setRelayState(true); else if (tempC < lowerLimit) setRelayState(false); } } }

/**
 * @brief Checks if the compressor protection timer is active and updates the 'protection' variable.
 */
void checkProtection() { if (localMode == true) { protection = false; } else { unsigned long protectionPeriod = (unsigned long)localProtectionTime * 1000UL; if (millis() - lastRelayChangeTime < protectionPeriod) { protection = true; } else { protection = false; } } if (protection != lastProtectionState) { printTimestamp(); Serial.print(">>> Protection status changed to: "); Serial.println(protection ? "Active" : "Inactive"); lastProtectionState = protection; } }

/**
 * @brief Controls the relay hardware and updates the relevant state variables.
 */
void setRelayState(bool newState) { if (relayState != newState) { relayState = newState; lastRelayChangeTime = millis(); printTimestamp(); Serial.print(">>> Relay state changed to: "); Serial.println(relayState ? "ON" : "OFF"); if (relayState) digitalWrite(RELAY_PIN, LOW); else digitalWrite(RELAY_PIN, HIGH); } }

/**
 * @brief Formats and displays a temperature value on a 3-digit block of the 7-segment display.
 */
void formatThreeDigit(float value, int p1, int p2, int p3) { if (value >= 100 || value <= -100) { int v = abs((int)value); lc.setDigit(0, p1, (v / 100) % 10, 0); lc.setDigit(0, p2, (v / 10) % 10, 0); lc.setDigit(0, p3, v % 10, 0); } else if (value >= 0) { int t = (int)value / 10; if (t == 0) lc.setChar(0, p1, ' ', 0); else lc.setDigit(0, p1, t, 0); lc.setDigit(0, p2, (int)value % 10, 1); lc.setDigit(0, p3, (int)(value * 10) % 10, 0); } else if (value > -10) { float a = -value; lc.setChar(0, p1, '-', 0); lc.setDigit(0, p2, (int)a % 10, 1); lc.setDigit(0, p3, (int)(a * 10) % 10, 0); } else { int a = (int)-value; lc.setChar(0, p1, '-', 0); lc.setDigit(0, p2, (a / 10) % 10, 0); lc.setDigit(0, p3, a % 10, 0); } }

/**
 * @brief Updates the entire 8-digit display with the current thermostat status.
 */
void updateDisplay(float tempC) { lc.clearDisplay(0); char m = localMode ? 'H' : 'C'; lc.setChar(0, 7, m, protection); formatThreeDigit(localSetpoint, 6, 5, 4); lc.setChar(0, 3, ' ', 0); formatThreeDigit(tempC, 2, 1, 0); }

/**
 * @brief Displays 'Err' on the temperature portion of the display if a sensor read fails.
 */
void displayError() { lc.setChar(0, 2, ' ', 0); lc.setChar(0, 1, 'E', 0); lc.setChar(0, 0, 'r', 'r'); }
