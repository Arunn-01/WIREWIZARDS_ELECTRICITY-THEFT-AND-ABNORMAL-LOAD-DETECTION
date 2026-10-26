/*
  ==================================================================================
  Project: Electricity Theft & Abnormal Load Detection System
  Target Hardware: ESP32 Dev Module
  Sensors & Peripherals:
    - PZEM-004T v3.0 (AC Energy Meter)
    - External Current Transformer (CT - 100A)
    - 16x2 I2C LCD Display (Address 0x27 or 0x3F)
    - Test Load: Incandescent / LED Bulb & Bulb Holder
    - Status Indicators: Onboard LED (GPIO 2) / Optional Active Buzzer (GPIO 4)
  ==================================================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PZEM004Tv30.h>

// ------------------------------ PIN DEFINITIONS ---------------------------------
#define PZEM_RX_PIN 16  // Connect to PZEM TX
#define PZEM_TX_PIN 17  // Connect to PZEM RX
#define ONBOARD_LED_PIN 2   // Built-in LED on ESP32
#define BUZZER_PIN      4   // Optional active buzzer

// --------------------------- THRESHOLD CONFIGURATION ----------------------------
// Set this to your test bulb's rated wattage:
const float RATED_LOAD_WATTS       = 60.0;   // Example: 60W bulb
const float THEFT_MARGIN_PERCENT   = 35.0;   // Trigger threshold: +35% above rated load
const float MIN_DETECTABLE_WATTS   = 3.0;    // Threshold below which circuit is considered idle

const float OVERLOAD_THEFT_LIMIT   = RATED_LOAD_WATTS * (1.0 + (THEFT_MARGIN_PERCENT / 100.0));
const float VOLTAGE_MIN_NORMAL     = 180.0;  // Undervoltage limit (V)
const float VOLTAGE_MAX_NORMAL     = 250.0;  // Overvoltage limit (V)
const float ABNORMAL_LOW_PF        = 0.60;   // Low power factor tamper limit

// -------------------------------- SYSTEM OBJECTS --------------------------------
PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2); // Change to 0x3F if your I2C adapter uses that address

enum SystemStatus {
  STATUS_IDLE,
  STATUS_NORMAL,
  STATUS_THEFT,
  STATUS_ABNORMAL_PF,
  STATUS_VOLTAGE_FAULT
};

SystemStatus currentStatus = STATUS_IDLE;

unsigned long lastPzemReadTime = 0;
const unsigned long PZEM_READ_INTERVAL = 1000;

unsigned long lastLcdSwitchTime = 0;
const unsigned long LCD_PAGE_INTERVAL = 3000;
uint8_t lcdPage = 0;

float voltage     = 0.0;
float current     = 0.0;
float power       = 0.0;
float energy      = 0.0;
float frequency   = 0.0;
float powerFactor = 0.0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ONBOARD_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(ONBOARD_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Theft & Anomaly");
  lcd.setCursor(0, 1);
  lcd.print("Detection Init..");
  delay(2000);
  lcd.clear();

  Serial.println("[INFO] System Initialized.");
  Serial.printf("[CONFIG] Rated Load: %.1f W | Theft Limit: > %.1f W\n\n", 
                RATED_LOAD_WATTS, OVERLOAD_THEFT_LIMIT);
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastPzemReadTime >= PZEM_READ_INTERVAL) {
    lastPzemReadTime = currentMillis;
    readSensors();
    analyzeLoadAndTheft();
    handleAlertOutputs();
    logDataToSerial();
  }

  if (currentMillis - lastLcdSwitchTime >= LCD_PAGE_INTERVAL) {
    lastLcdSwitchTime = currentMillis;
    lcdPage = (lcdPage + 1) % 2;
    updateDisplay();
  }
}

void readSensors() {
  float v  = pzem.voltage();
  float i  = pzem.current();
  float p  = pzem.power();
  float e  = pzem.energy();
  float f  = pzem.frequency();
  float pf = pzem.pf();

  voltage     = isnan(v)  ? 0.0 : v;
  current     = isnan(i)  ? 0.0 : i;
  power       = isnan(p)  ? 0.0 : p;
  energy      = isnan(e)  ? 0.0 : e;
  frequency   = isnan(f)  ? 0.0 : f;
  powerFactor = isnan(pf) ? 0.0 : pf;
}

void analyzeLoadAndTheft() {
  if (voltage > 0 && (voltage < VOLTAGE_MIN_NORMAL || voltage > VOLTAGE_MAX_NORMAL)) {
    currentStatus = STATUS_VOLTAGE_FAULT;
    return;
  }
  if (power < MIN_DETECTABLE_WATTS) {
    currentStatus = STATUS_IDLE;
    return;
  }
  if (power > OVERLOAD_THEFT_LIMIT) {
    currentStatus = STATUS_THEFT;
    return;
  }
  if (powerFactor > 0.0 && powerFactor < ABNORMAL_LOW_PF) {
    currentStatus = STATUS_ABNORMAL_PF;
    return;
  }
  currentStatus = STATUS_NORMAL;
}

void handleAlertOutputs() {
  if (currentStatus == STATUS_THEFT || currentStatus == STATUS_ABNORMAL_PF) {
    digitalWrite(ONBOARD_LED_PIN, (millis() / 250) % 2); 
    digitalWrite(BUZZER_PIN, HIGH);
  } else if (currentStatus == STATUS_VOLTAGE_FAULT) {
    digitalWrite(ONBOARD_LED_PIN, (millis() / 500) % 2);
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    digitalWrite(ONBOARD_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void updateDisplay() {
  lcd.clear();

  if (currentStatus == STATUS_THEFT) {
    lcd.setCursor(0, 0);
    lcd.print("! THEFT ALERT !");
    lcd.setCursor(0, 1);
    lcd.printf("P:%.1fW (EXCESS)", power);
    return;
  } 
  if (currentStatus == STATUS_ABNORMAL_PF) {
    lcd.setCursor(0, 0);
    lcd.print("! ABNORMAL LOAD !");
    lcd.setCursor(0, 1);
    lcd.printf("PF:%.2f (LOW PF)", powerFactor);
    return;
  }
  if (currentStatus == STATUS_VOLTAGE_FAULT) {
    lcd.setCursor(0, 0);
    lcd.print("! VOLTAGE FAULT !");
    lcd.setCursor(0, 1);
    lcd.printf("V: %.1fV Check Mains", voltage);
    return;
  }

  if (lcdPage == 0) {
    lcd.setCursor(0, 0);
    lcd.printf("V:%.1fV  I:%.2fA", voltage, current);
    lcd.setCursor(0, 1);
    lcd.printf("P:%.1fW  PF:%.2f", power, powerFactor);
  } else {
    lcd.setCursor(0, 0);
    lcd.printf("Energy:%.3fkWh", energy);
    lcd.setCursor(0, 1);
    lcd.print(currentStatus == STATUS_IDLE ? "Status: STANDBY" : "Status: NORMAL OK");
  }
}

void logDataToSerial() {
  Serial.printf("V: %.1f V | I: %.3f A | P: %.1f W | E: %.3f kWh | PF: %.2f | Status: ", 
                voltage, current, power, energy, powerFactor);
  switch (currentStatus) {
    case STATUS_IDLE:        Serial.println("[IDLE]"); break;
    case STATUS_NORMAL:      Serial.println("[NORMAL OK]"); break;
    case STATUS_THEFT:       Serial.println("[*** THEFT DETECTED - EXCESS LOAD ***]"); break;
    case STATUS_ABNORMAL_PF: Serial.println("[*** ABNORMAL LOAD - LOW PF ***]"); break;
    case STATUS_VOLTAGE_FAULT: Serial.println("[*** VOLTAGE FAULT ***]"); break;
  }
}
