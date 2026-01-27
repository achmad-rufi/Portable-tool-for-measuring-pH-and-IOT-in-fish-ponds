// ==========================================
// KODE FINAL: PH PETANI (BATTERY MONITOR & MANUAL IOT)
// ==========================================

// --- 1. KONFIGURASI BLYNK ---
#define BLYNK_TEMPLATE_ID "TMPL66VNal4OI"
#define BLYNK_TEMPLATE_NAME "Portable PH dan water level"
#define BLYNK_AUTH_TOKEN "PqB_ai3MV3L_TvBQk4_HuxRMmR2xjaQI"
// ==========================================
// KODE FINAL: PH PETANI + BATERAI TERKALIBRASI (1.09)
// ==========================================

// --- 1. KONFIGURASI BLYNK --

#define BLYNK_PRINT Serial
#define TINY_GSM_MODEM_SIM800

// --- 2. LIBRARY ---
#include <TinyGsmClient.h>
#include <BlynkSimpleTinyGSM.h>
#include <LiquidCrystal_I2C.h>
#include <NewPing.h>
#include <Wire.h>
#include <Preferences.h> 

// --- 3. KONEKSI (INDOSAT) ---
char auth[] = BLYNK_AUTH_TOKEN;
char apn[]  = "indosatgprs";
char user[] = "indosat";
char pass[] = "indosat";

// PIN SERIAL GSM (FIX: 26 & 27)
#define RX_GSM 26
#define TX_GSM 27

HardwareSerial SerialGSM(2);
TinyGsm modem(SerialGSM);
BlynkTimer timer;
Preferences preferences; 

// --- 4. DEFINISI PIN (SAFE PINS) ---
#define PIN_RELAY1 4   // Relay Sistem
#define PIN_RELAY2 18  // Relay Pompa
#define PIN_LED_R  15  
#define PIN_LED_G  14  
#define PIN_LED_B  2   
#define PIN_BUZZER 13

// SENSOR
#define PIN_TRIG 5
#define PIN_ECHO 19    
#define MAX_DISTANCE 200 
#define PIN_PH    36   // VP
#define PIN_BATT  34   // Pin 34 (Monitoring Baterai)

// INPUT TOMBOL (Active LOW)
#define BTN_SAWAH 32   // Dual Fungsi: Sawah & IoT (>5s)
#define BTN_SUMUR 33
#define BTN_KOLAM 25
#define BTN_OK    23   
#define BTN_CAL_PIN 17 // Tombol Kalibrasi

// --- 5. OBJEK & VARIABEL GLOBAL ---
LiquidCrystal_I2C lcd(0x27, 16, 2); 
NewPing sonar(PIN_TRIG, PIN_ECHO, MAX_DISTANCE);

enum SystemMode { MODE_MANUAL, MODE_IOT_INIT, MODE_IOT_RUN };
enum ManualState { SAWAH, SUMUR, KOLAM };

volatile bool interruptTriggered = false;
volatile int pendingModeSelect = -1; 

SystemMode currentSysMode = MODE_MANUAL; 
ManualState currentManualState = SAWAH; 

float phValue = 0.0;
float calibrationOffset = 0.0;
unsigned long iotStartTime = 0;
bool lcdOffIoT = false;
int relay2State = HIGH; 

// Batas pH
float limitBawah = 6.0;
float limitAtas = 8.0;

// Variabel Buzzer
unsigned long buzzerStartTime = 0;
bool buzzerActive = false;
bool isAbnormalState = false;

// --- 6. FUNCTION PROTOTYPES ---
void handleButtonPress();
void executeStateChange(int modeID);
void runManualMode();
void runIoTMode();
void toggleRelay2();
void updateLimits();
void controlAlerts(float ph);
float readRawPH();
// [UPDATE] Prototipe fungsi baterai baru
float readBatteryVoltage();
int getBatteryPercentage(float voltage); 
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max);

void sendDataToBlynk();
void loadCalibration();
void saveCalibration();

// --- 7. ISRs ---
void IRAM_ATTR isrSawah() { pendingModeSelect = 0; interruptTriggered = true; }
void IRAM_ATTR isrSumur() { pendingModeSelect = 1; interruptTriggered = true; }
void IRAM_ATTR isrKolam() { pendingModeSelect = 2; interruptTriggered = true; }
void IRAM_ATTR isrCal()   { pendingModeSelect = 4; interruptTriggered = true; }

// --- 8. SETUP ---
void setup() {
  Serial.begin(115200);
  // Resolusi ADC diset eksplisit biar sama dengan kode tes Anda
  analogReadResolution(12); 
  
  SerialGSM.begin(9600, SERIAL_8N1, RX_GSM, TX_GSM); 

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0); lcd.print("System Booting..");
  
  loadCalibration();
  delay(1000);

  // Init Pin
  pinMode(PIN_RELAY1, OUTPUT); digitalWrite(PIN_RELAY1, HIGH);
  pinMode(PIN_RELAY2, OUTPUT); digitalWrite(PIN_RELAY2, HIGH);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  pinMode(BTN_SAWAH, INPUT_PULLUP);
  pinMode(BTN_SUMUR, INPUT_PULLUP);
  pinMode(BTN_KOLAM, INPUT_PULLUP);
  pinMode(BTN_OK,    INPUT_PULLUP);
  pinMode(BTN_CAL_PIN, INPUT_PULLUP); 

  attachInterrupt(digitalPinToInterrupt(BTN_SAWAH), isrSawah, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_SUMUR), isrSumur, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_KOLAM), isrKolam, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_CAL_PIN), isrCal, FALLING);

  lcd.clear();
  timer.setInterval(5000L, sendDataToBlynk);
}

// --- 9. LOOP UTAMA ---
void loop() {
  if (interruptTriggered) {
    handleButtonPress(); 
  }

  if (currentSysMode == MODE_MANUAL) {
    runManualMode();
  } else {
    runIoTMode();
  }
}

// --- 10. LOGIC TOMBOL ---
void handleButtonPress() {
  int pinTarget = -1;
  String namaMode = "";
  
  lcd.backlight(); lcdOffIoT = false; 

  int selectedModeID = pendingModeSelect;

  switch(selectedModeID) {
    case 0: pinTarget = BTN_SAWAH; namaMode = "Mode SAWAH"; break;
    case 1: pinTarget = BTN_SUMUR; namaMode = "Mode SUMUR"; break;
    case 2: pinTarget = BTN_KOLAM; namaMode = "Mode KOLAM"; break;
    case 4: pinTarget = BTN_CAL_PIN; namaMode = "Kalibrasi"; break;
    default: interruptTriggered = false; return;
  }

  lcd.clear(); lcd.print("TAHAN TOMBOL...");
  digitalWrite(PIN_BUZZER, HIGH); delay(50); digitalWrite(PIN_BUZZER, LOW);

  unsigned long startHold = millis();
  bool actionReady = false;
  bool specialAction = false;

  while (digitalRead(pinTarget) == LOW) {
    if(currentSysMode == MODE_IOT_RUN) Blynk.run(); 
    unsigned long duration = millis() - startHold;

    if (duration > 3000 && duration < 5000 && !actionReady) {
      actionReady = true;
      digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW);
      lcd.clear();
      if (selectedModeID == 4) {
        lcd.print("Lepas: Kalibrasi"); lcd.setCursor(0,1); lcd.print("Tahan: Pompa");
      } else if (selectedModeID == 0) {
        lcd.print("Lepas: SAWAH"); lcd.setCursor(0,1); lcd.print("Tahan: IOT >5d");
      } else {
        lcd.print("LEPAS -> OK"); lcd.setCursor(0,1); lcd.print(namaMode);
      }
    }

    if (duration > 5000) {
      if (selectedModeID == 4) { // CAL -> Pompa
        toggleRelay2(); specialAction = true;
        while(digitalRead(pinTarget) == LOW) { delay(10); } break;
      }
      if (selectedModeID == 0) { // Sawah -> IoT
        selectedModeID = 3; namaMode = "Mode IOT";
        for(int i=0; i<2; i++){ digitalWrite(PIN_BUZZER, HIGH); delay(50); digitalWrite(PIN_BUZZER, LOW); delay(50); }
        lcd.clear(); lcd.print("CHANGE TO IOT!"); specialAction = true;
        while(digitalRead(pinTarget) == LOW) { delay(10); }
        executeStateChange(3); 
        interruptTriggered = false; pendingModeSelect = -1; return; 
      }
    }
    delay(10);
  }

  if (specialAction) {
    interruptTriggered = false; pendingModeSelect = -1; lcd.clear(); return;
  }

  if (actionReady) {
    lcd.clear(); lcd.print("Konfirmasi: OK?"); lcd.setCursor(0,1); lcd.print("-> "); lcd.print(namaMode);
    unsigned long waitOK = millis(); bool confirmed = false;
    while(millis() - waitOK < 5000) {
      if(currentSysMode == MODE_IOT_RUN) Blynk.run();
      if (digitalRead(BTN_OK) == LOW) { confirmed = true; break; }
    }
    if (confirmed) executeStateChange(selectedModeID);
    else { lcd.clear(); lcd.print("BATAL"); delay(1000); }
  } 
  interruptTriggered = false; pendingModeSelect = -1; lcd.clear();
}

void executeStateChange(int modeID) {
  digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW);
  if (modeID == 4) { 
    lcd.clear(); lcd.print("Kalibrasi...");
    float totalVoltage = 0;
    for(int i=0; i<10; i++){ totalVoltage += (analogRead(PIN_PH) * (3.3 / 4095.0)); delay(20); }
    float avgVoltage = totalVoltage / 10.0;
    float rawPH = 3.5 * avgVoltage; 
    calibrationOffset = 6.86 - rawPH; 
    saveCalibration(); 
    lcd.clear(); lcd.print("Kalibrasi OK!"); delay(2000);
  } else if (modeID == 3) { 
    if(currentSysMode != MODE_IOT_RUN && currentSysMode != MODE_IOT_INIT) {
      currentSysMode = MODE_IOT_INIT; iotStartTime = millis();
      lcd.clear(); lcd.print("Connecting IoT.."); lcd.setCursor(0,1); lcd.print("Indosat...");
      digitalWrite(PIN_RELAY1, LOW); 
      modem.restart(); Blynk.begin(auth, modem, apn, user, pass);
    }
  } else { 
    if (currentSysMode == MODE_IOT_RUN || currentSysMode == MODE_IOT_INIT) {
       lcd.clear(); lcd.print("Disconnecting..."); Blynk.disconnect(); 
       lcdOffIoT = false; lcd.backlight(); digitalWrite(PIN_RELAY1, HIGH); 
    }
    currentSysMode = MODE_MANUAL;
    if (modeID == 0) currentManualState = SAWAH;
    else if (modeID == 1) currentManualState = SUMUR;
    else if (modeID == 2) currentManualState = KOLAM;
  }
}
// --- FUNGSI BACA BATERAI (REVISI KALIBRASI) ---
float readBatteryVoltage() {
  // 1. Averaging 10x sampel
  long sumADC = 0;
  for (int i = 0; i < 100; i++) {
    sumADC += analogRead(PIN_BATT);
    delay(10);
  }
  float avgADC = sumADC / 100.0;
  
  // 2. Faktor Kalibrasi BARU (Hasil Perhitungan: 3.56 / 5.83)
  float vPin = (avgADC * 3.3) / 4095.0;
  float calibrationFactor = 1.5; // <--- GANTI JADI INI
  float dividerRatio = 2.0;       
  
  return vPin * dividerRatio * calibrationFactor;
}

int getBatteryPercentage(float voltage) {
  // Logic Li-Ion Map yang Anda buat (Sangat Bagus!)
  if (voltage >= 4.2) return 100;
  else if (voltage >= 3.7) return (int)mapFloat(voltage, 3.7, 4.2, 50, 100);
  else if (voltage >= 3.2) return (int)mapFloat(voltage, 3.2, 3.7, 0, 50);
  else return 0;
}

// Helper untuk mapping float
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void sendDataToBlynk() {
  if (Blynk.connected()) {
    float ph = readRawPH() + calibrationOffset;
    int distance = sonar.ping_cm();
    float volt = readBatteryVoltage();
    int persen = getBatteryPercentage(volt);

    Blynk.virtualWrite(V1, ph); 
    Blynk.virtualWrite(V2, distance); 
    Blynk.virtualWrite(V5, volt);   
    Blynk.virtualWrite(V6, persen); 
  }
}

void toggleRelay2() {
  relay2State = !relay2State; 
  digitalWrite(PIN_RELAY2, relay2State);
  if(currentSysMode == MODE_IOT_RUN && Blynk.connected()){
     Blynk.virtualWrite(V4, (relay2State == LOW) ? 1 : 0);
  }
  lcd.clear(); lcd.print("POMPA AIR"); lcd.setCursor(0,1); lcd.print(relay2State == LOW ? "ON" : "OFF"); delay(1000); 
}

void runManualMode() {
  updateLimits(); 
  phValue = readRawPH() + calibrationOffset;
  lcd.setCursor(0, 0); lcd.print("Md:");
  if(currentManualState == SAWAH) lcd.print("SAWAH");
  else if(currentManualState == SUMUR) lcd.print("SUMUR");
  else if(currentManualState == KOLAM) lcd.print("KOLAM");
  
  // Tampilkan Persen Baterai di Manual Mode (Pojok Kanan Atas)
  // Biar keren, kita tampilkan bergantian atau di pojok
  int batt = getBatteryPercentage(readBatteryVoltage());
  lcd.setCursor(12,0); lcd.print(batt); lcd.print("%");

  lcd.setCursor(0, 1); lcd.print("pH:"); lcd.print(phValue, 1); 
  controlAlerts(phValue);
  delay(100);
}

void runIoTMode() {
  if (interruptTriggered) return;
  Blynk.run(); timer.run();
  if (currentSysMode == MODE_IOT_INIT) {
    if (millis() - iotStartTime > 20000 && !lcdOffIoT) {
      lcd.noBacklight(); lcd.clear(); lcdOffIoT = true;
      currentSysMode = MODE_IOT_RUN;
    }
    if (!lcdOffIoT) {
      float v = readBatteryVoltage();
      int p = getBatteryPercentage(v);
      lcd.setCursor(0,0); lcd.print("IoT ON | "); lcd.print(p); lcd.print("%");
      lcd.setCursor(0,1); lcd.print("pH:"); lcd.print(readRawPH()+calibrationOffset, 1);
    }
  }
}

void updateLimits() {
  if(currentManualState == SAWAH) { limitBawah = 5.5; limitAtas = 7.0; }
  else if(currentManualState == SUMUR) { limitBawah = 6.5; limitAtas = 8.5; }
  else { limitBawah = 7.0; limitAtas = 8.0; }
}

void controlAlerts(float ph) {
  digitalWrite(PIN_LED_R, LOW); digitalWrite(PIN_LED_G, LOW); digitalWrite(PIN_LED_B, LOW);
  bool bad = false;
  if (ph < limitBawah) { 
    digitalWrite(PIN_LED_R, HIGH); lcd.setCursor(9, 1); lcd.print(" ASAM! "); bad = true; 
    static unsigned long t=0; if(millis()-t>3000){lcd.setCursor(0,0);lcd.print("Add Kapur!"); t=millis();}
  } 
  else if (ph > limitAtas) { 
    digitalWrite(PIN_LED_B, HIGH); lcd.setCursor(9, 1); lcd.print(" BASA! "); bad = true; 
  } 
  else { 
    digitalWrite(PIN_LED_G, HIGH); lcd.setCursor(9, 1); lcd.print(" AMAN  "); 
  }

  if (bad) {
    if (!isAbnormalState) {
      isAbnormalState = true; buzzerActive = true; buzzerStartTime = millis(); digitalWrite(PIN_BUZZER, HIGH); 
    } else if (buzzerActive && (millis() - buzzerStartTime > 5000)) {
      digitalWrite(PIN_BUZZER, LOW); buzzerActive = false; 
    }
  } else {
    isAbnormalState = false; buzzerActive = false; digitalWrite(PIN_BUZZER, LOW); 
  }
}

float readRawPH() {
  long total = 0;
  for(int i=0; i<10; i++) { total += analogRead(PIN_PH); delay(5); }
  float voltage = (total / 10.0) * (3.3 / 4095.0);
  return 3.5 * voltage; 
}

void loadCalibration() { preferences.begin("ph-data", true); calibrationOffset = preferences.getFloat("offset", 0.0); preferences.end(); }
void saveCalibration() { preferences.begin("ph-data", false); preferences.putFloat("offset", calibrationOffset); preferences.end(); }

BLYNK_WRITE(V3) { digitalWrite(PIN_RELAY1, (param.asInt() == 1) ? LOW : HIGH); }
BLYNK_WRITE(V4) { relay2State = (param.asInt() == 1) ? LOW : HIGH; digitalWrite(PIN_RELAY2, relay2State); }