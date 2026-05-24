#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <SPI.h>
#include <LittleFS.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ADS1X15.h>
#include <MPU9250_asukiaaa.h>
#include <vl53l4cd_class.h>

// ===============================
// WIFI AP SOLO EN MODO MATLAB
// ===============================
const char* ssid = "EMI_SCAN";
const char* password = "12345678";

WebServer server(80);
bool wifiActive = false;

void handleRoot();
void handleData();
void handleCalibrate();

// ===============================
// INICIAR WIFI AP
// ===============================
void startWiFiAP() {

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/calibrate", handleCalibrate);

  server.begin();

  wifiActive = true;
}


// ===============================
// TFT ST7789 240x320
// CONFIG QUE SI FUNCIONA
// ===============================
#define TFT_DC   13
#define TFT_RST  3
#define TFT_CS   10
#define TFT_SCLK 12
#define TFT_MOSI 11

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ===============================
// I2C ADS1115 + MPU9250
// ===============================
#define I2C_SDA 8
#define I2C_SCL 9

Adafruit_ADS1115 ads;
MPU9250_asukiaaa mpu;
VL53L4CD tof(&Wire, -1);

// ===============================
// BOTON
// ===============================
#define BTN_CAL 5

// ===============================
// ADS1115 / E-FIELD BJT
// ===============================
#define ADS_CHANNEL 0
#define SAMPLES 12
#define BASELINE_SAMPLES 300

float raw_mV = 0;
float filtered_mV = 0;
float baseline_mV = 0;
float signal_mV = 0;

float noiseFloor_mV = 8.0;
float maxSignal_mV = 250.0;
float sensitivity = 0.8;

int ePercent = 0;
bool adcSaturated = false;

// ===============================
// MPU9250 / H-FIELD
// ===============================
float ax = 0, ay = 0, az = 0;
float gx = 0, gy = 0, gz = 0;
float mx = 0, my = 0, mz = 0;

float magTotal = 0;
float magBaseline = 0;
float hSignal = 0;

// ===============================
// VL53L4CD / DISTANCIA ToF
// ===============================
int distance_mm = -1;
float distance_cm = -1.0;
bool tofOK = false;
bool tofValid = false;

float hNoiseFloor = 2.0;
float hMaxSignal = 80.0;
int hPercent = 0;

// ===============================
// EMI RELATIVO
// ===============================
int emiPercent = 0;

float eWeight = 0.65;
float hWeight = 0.35;

// ===============================
// TIEMPOS
// ===============================
unsigned long lastSample = 0;
unsigned long lastDraw = 0;
unsigned long lastButton = 0;
unsigned long buttonPressStart = 0;

bool buttonWasPressed = false;

int screenMode = 0;
int lastScreenMode = -1;

// 0 = E-FIELD + H-FIELD
// 1 = EMI RELATIVO
// 2 = MATLAB / WIFI

// ===============================
// COLORES
// ===============================
uint16_t levelColor(int p) {

  if (p < 10)
    return tft.color565(0, 40, 255);      // azul

  if (p < 25)
    return tft.color565(0, 255, 255);     // cyan

  if (p < 45)
    return tft.color565(0, 255, 80);      // verde

  if (p < 65)
    return tft.color565(255, 255, 0);     // amarillo

  if (p < 85)
    return tft.color565(255, 120, 0);     // naranja

  return tft.color565(255, 0, 0);         // rojo
}
// ===============================
// INTRO EMI SCAN - ONDAS E/H
// ===============================

// ===============================
// INTRO EMI SCAN - EM WAVE 3D SIN PARPADEO
// ===============================
void introAnimation() {

  const int W = 320;
  const int H = 240;

  float phase = 0;

  uint16_t gridColor = tft.color565(15, 28, 45);
  uint16_t axisColor = tft.color565(70, 255, 120);

  uint16_t eColor = ST77XX_RED;
  uint16_t hColor = ST77XX_CYAN;

  // ==========================
  // FONDO FIJO SOLO UNA VEZ
  // ==========================
  tft.fillScreen(ST77XX_BLACK);

  // GRID TECNICO
  for (int gy = 24; gy < H; gy += 28) {
    tft.drawFastHLine(0, gy, W, gridColor);
  }

  for (int gx = 0; gx < W; gx += 32) {
    tft.drawFastVLine(gx, 24, 220, gridColor);
  }

  // TITULO
  tft.setTextSize(4);

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(42, 12);
  tft.print("EMI");

  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(135, 12);
  tft.print("SCAN");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(78, 48);
  tft.print("ELECTROMAGNETIC FIELD");

  // LABELS FIJOS
  tft.setTextSize(2);

  tft.setTextColor(eColor);
  tft.setCursor(18, 172);
  tft.print("E");

  tft.setTextColor(hColor);
  tft.setCursor(18, 200);
  tft.print("H");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(42, 176);
  tft.print("Campo electrico");

  tft.setCursor(42, 204);
  tft.print("Campo magnetico");

  // MARCO BARRA FIJO
  tft.drawRect(40, 224, 240, 8, ST77XX_WHITE);

  // ==========================
  // ANIMACION PRINCIPAL
  // ==========================
  for (int frame = 0; frame < 180; frame++) {

    // Borrar SOLO zona dinámica, no toda la pantalla
    tft.fillRect(0, 72, 320, 95, ST77XX_BLACK);
    tft.fillRect(41, 225, 238, 6, ST77XX_BLACK);

    // Redibujar grid solo dentro de zona dinámica
    for (int gy = 80; gy < 168; gy += 28) {
      tft.drawFastHLine(0, gy, W, gridColor);
    }

    for (int gx = 0; gx < W; gx += 32) {
      tft.drawFastVLine(gx, 72, 95, gridColor);
    }

    // ==========================
    // EJE CENTRAL INCLINADO
    // ==========================
    int lastAX = -20;
    int lastAY = 128 + (-0.18 * (lastAX - 160));

    for (int x = -20; x <= 340; x += 6) {
      int y = 128 + (-0.18 * (x - 160));
      tft.drawLine(lastAX, lastAY, x, y, axisColor);
      lastAX = x;
      lastAY = y;
    }

    // Flecha propagacion
    int fx1 = 270;
    int fy1 = 128 + (-0.18 * (270 - 160));

    int fx2 = 305;
    int fy2 = 128 + (-0.18 * (305 - 160));

    tft.drawLine(fx1, fy1, fx2, fy2, axisColor);

    tft.fillTriangle(
      fx2, fy2,
      fx2 - 10, fy2 - 5,
      fx2 - 7, fy2 + 8,
      axisColor
    );

    // ==========================
    // ONDAS E Y H
    // ==========================
    int lastEX = -1;
    int lastEY = -1;

    int lastHX = -1;
    int lastHY = -1;

    for (int x = -10; x <= 330; x += 6) {

      float centerY = 128 + (-0.18 * (x - 160));
      float angle = (x * 0.065) + phase;

      float perspective = 0.70 + (x / 420.0);

      // Campo electrico E
      float eWave = sin(angle);
      int eY = centerY - (eWave * 52 * perspective);

      if (lastEX >= 0) {
        tft.drawLine(lastEX, lastEY, x, eY, eColor);
      }

      tft.drawLine(x, centerY, x, eY, eColor);

      if (x % 18 == 0) {
        tft.fillCircle(x, eY, 2, eColor);
      }

      lastEX = x;
      lastEY = eY;

      // Campo magnetico H
      float hWave = cos(angle);

      int hX = x + (hWave * 28 * perspective);
      int hY = centerY + (hWave * 14 * perspective);

      if (lastHX >= 0) {
        tft.drawLine(lastHX, lastHY, hX, hY, hColor);
      }

      tft.drawLine(x, centerY, hX, hY, hColor);

      if (x % 18 == 0) {
        tft.fillCircle(hX, hY, 2, hColor);
      }

      lastHX = hX;
      lastHY = hY;
    }

    // ==========================
    // BARRA DE CARGA
    // ==========================
    int bar = map(frame, 0, 179, 0, 238);
    tft.fillRect(41, 225, bar, 6, ST77XX_CYAN);

    phase += 0.11;

    delay(12);
  }

  tft.fillScreen(ST77XX_BLACK);


  // ==========================================
  // READY SCREEN
  // ==========================================
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(4);

  tft.setCursor(55, 62);
  tft.print("EMI");

  tft.setCursor(135, 62);
  tft.print("SCAN");

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);

  tft.setCursor(72, 115);
  tft.print("SYSTEM READY");

  tft.drawFastHLine(45, 165, 230, ST77XX_CYAN);

  tft.drawLine(120, 165, 135, 165, ST77XX_CYAN);
  tft.drawLine(135, 165, 145, 145, ST77XX_CYAN);
  tft.drawLine(145, 145, 155, 185, ST77XX_CYAN);
  tft.drawLine(155, 185, 168, 160, ST77XX_CYAN);
  tft.drawLine(168, 160, 180, 165, ST77XX_CYAN);

  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(1);

  tft.setCursor(58, 205);
  tft.print("E-FIELD + H-FIELD + ToF");

  delay(1800);
}


// ===============================
// LECTURA ADS1115
// ===============================
float readADS_mV() {
  long sum = 0;

  for (int i = 0; i < SAMPLES; i++) {
    sum += ads.readADC_SingleEnded(ADS_CHANNEL);
    delayMicroseconds(500);
  }

  float raw = sum / (float)SAMPLES;

  // GAIN_ONE = +-4.096V = 0.125mV por bit
  return raw * 0.125;
}

// ===============================
// ACTUALIZAR MPU
// ===============================
void updateMPU() {
  mpu.accelUpdate();
  mpu.gyroUpdate();
  mpu.magUpdate();

  ax = mpu.accelX();
  ay = mpu.accelY();
  az = mpu.accelZ();

  gx = mpu.gyroX();
  gy = mpu.gyroY();
  gz = mpu.gyroZ();

  mx = mpu.magX();
  my = mpu.magY();
  mz = mpu.magZ();

  magTotal = sqrt(mx * mx + my * my + mz * mz);
}

// ===============================
// ACTUALIZAR SENSOR ToF VL53L4CD
// ===============================
void updateToF() {
  if (!tofOK) {
    tofValid = false;
    distance_mm = -1;
    distance_cm = -1.0;
    return;
  }

  VL53L4CD_Result_t results;
  uint8_t dataReady = 0;

  tof.VL53L4CD_CheckForDataReady(&dataReady);

  if (dataReady) {
    tof.VL53L4CD_GetResult(&results);
    tof.VL53L4CD_ClearInterrupt();

    if (results.range_status == 0) {
      distance_mm = results.distance_mm;
      distance_cm = distance_mm / 10.0;
      tofValid = true;
    } else {
      distance_mm = -1;
      distance_cm = -1.0;
      tofValid = false;
    }
  }
}

// ===============================
// CALIBRACION E/H
// ===============================
void calibrateBaseline() {
  float eSum = 0;
  float hSum = 0;

  float minE = 9999;
  float maxE = 0;

  float minH = 9999;
  float maxH = 0;

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(35, 45);
  tft.print("EMI-SCAN");

  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(35, 80);
  tft.print("CALIBRANDO...");

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(30, 115);
  tft.print("No tocar antena ni mover sensor");

  tft.setCursor(45, 130);
  tft.print("Midiendo ambiente E/H...");

  tft.drawRect(50, 165, 220, 16, ST77XX_WHITE);

  for (int i = 0; i < BASELINE_SAMPLES; i++) {
    float e = readADS_mV();
    updateMPU();
    float h = magTotal;

    eSum += e;
    hSum += h;

    if (e < minE) minE = e;
    if (e > maxE) maxE = e;

    if (h < minH) minH = h;
    if (h > maxH) maxH = h;

    if (i % 5 == 0) {
      int bar = map(i, 0, BASELINE_SAMPLES, 0, 216);
      tft.fillRect(52, 167, bar, 12, ST77XX_GREEN);

      tft.fillRect(90, 195, 150, 18, ST77XX_BLACK);
      tft.setTextColor(ST77XX_CYAN);
      tft.setTextSize(1);
      tft.setCursor(95, 200);
      tft.print("Baseline ");
      tft.print((i * 100) / BASELINE_SAMPLES);
      tft.print("%");
    }

    delay(3);
  }

  baseline_mV = eSum / BASELINE_SAMPLES;
  filtered_mV = baseline_mV;

  magBaseline = hSum / BASELINE_SAMPLES;

  float noiseSpanE = maxE - minE;
  float noiseSpanH = maxH - minH;

  noiseFloor_mV = noiseSpanE + 4.0;
  noiseFloor_mV = constrain(noiseFloor_mV, 8.0, 25.0);

  maxSignal_mV = noiseFloor_mV * 30.0;
  maxSignal_mV = constrain(maxSignal_mV, 120.0, 500.0);

  hNoiseFloor = noiseSpanH + 1.5;
  hNoiseFloor = constrain(hNoiseFloor, 2.0, 15.0);

  hMaxSignal = hNoiseFloor * 20.0;
  hMaxSignal = constrain(hMaxSignal, 40.0, 200.0);

  tft.fillRect(50, 220, 240, 20, ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(70, 225);
  tft.print("CALIBRADO AMBIENTAL");

  delay(700);

  lastScreenMode = -1;

//  Serial.println("===== CALIBRACION EMI-SCAN =====");
//  Serial.print("E BASE mV=");
//  Serial.println(baseline_mV);
//  Serial.print("E NOISE mV=");
//  Serial.println(noiseFloor_mV);
//  Serial.print("E MAX mV=");
//  Serial.println(maxSignal_mV);
//  Serial.print("H BASE uT=");
//  Serial.println(magBaseline);
//  Serial.print("H NOISE uT=");
//  Serial.println(hNoiseFloor);
//  Serial.print("H MAX uT=");
//  Serial.println(hMaxSignal);
//  Serial.println("================================");
}

// ===============================
// ACTUALIZAR E-FIELD
// ===============================
void updateEField() {
  raw_mV = readADS_mV();

  adcSaturated = raw_mV >= 3250.0;

  filtered_mV = (filtered_mV * 0.94) + (raw_mV * 0.06);

  // BJT: reposo alto, campo E baja voltaje
  signal_mV = baseline_mV - filtered_mV;
  if (signal_mV < 0) signal_mV = 0;

  if (signal_mV < noiseFloor_mV) {
    ePercent = 0;
  } else {
    float amplified = signal_mV * sensitivity;
    ePercent = map((int)(amplified * 100),
                   (int)(noiseFloor_mV * 100),
                   (int)(maxSignal_mV * 100),
                   0, 100);
    ePercent = constrain(ePercent, 0, 100);
  }

  // Baseline dinámico solo en reposo
  if (ePercent < 5) {
    baseline_mV = (baseline_mV * 0.9995) + (filtered_mV * 0.0005);
  }
}

// ===============================
// ACTUALIZAR H-FIELD
// ===============================
void updateHField() {
  hSignal = abs(magTotal - magBaseline);

  if (hSignal < hNoiseFloor) {
    hPercent = 0;
  } else {
    hPercent = map((int)(hSignal * 100),
                   (int)(hNoiseFloor * 100),
                   (int)(hMaxSignal * 100),
                   0, 100);
    hPercent = constrain(hPercent, 0, 100);
  }

  // Baseline magnético lento si no hay variación
  if (hPercent < 5) {
    magBaseline = (magBaseline * 0.9995) + (magTotal * 0.0005);
  }
}

// ===============================
// EMI RELATIVO
// ===============================
void updateEMIIndex() {
  float eNorm = ePercent / 100.0;
  float hNorm = hPercent / 100.0;

  float combined = (eWeight * eNorm) + (hWeight * hNorm);

  // Realce no lineal ligero para eventos simultáneos E/H
  float couplingBoost = eNorm * hNorm * 0.25;

  float emi = combined + couplingBoost;
  if (emi > 1.0) emi = 1.0;

  emiPercent = (int)(emi * 100.0);
}

// ===============================
// WIFI CONTROL
// ===============================
void handleRoot();
void handleData();
void handleCalibrate();

void enableWiFi() {
  if (!wifiActive) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);

    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/calibrate", handleCalibrate);
    server.begin();

    wifiActive = true;
    Serial.println("WiFi AP ON");
  }
}

void disableWiFi() {
  if (wifiActive) {
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiActive = false;
    Serial.println("WiFi AP OFF");
  }
}

// ===============================
// UI BASE
// ===============================
void drawBaseUI(const char* title) {

  tft.fillScreen(ST77XX_BLACK);

  // ===== TITULO PRINCIPAL =====
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(4);

  const char* mainTitle = "EMI SCAN";

  int16_t x1, y1;
  uint16_t w, h;

  tft.getTextBounds(mainTitle, 0, 0, &x1, &y1, &w, &h);

  int centeredX = (320 - w) / 2;

  tft.setCursor(centeredX, 8);
  tft.print(mainTitle);

  // ===== SUBTITULO =====
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.setCursor(20, 42);
  tft.print(title);

  // ===== FOOTER =====
  tft.setTextColor(0xC618);
  tft.setCursor(20, 215);
  tft.print("PUSH BUTTON: change page | Hold 5s: recalibrar");
}

// ===============================
// MODO 0: E + H DOS COLUMNAS
// ===============================
void drawEHBase() {
  drawBaseUI("E-FIELD + H-FIELD");

  tft.drawLine(160, 65, 160, 200, ST77XX_BLUE);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(30, 70);
  tft.print("E-FIELD");

  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(195, 70);
  tft.print("H-FIELD");

  tft.drawRect(20, 165, 120, 18, ST77XX_WHITE);
  tft.drawRect(180, 165, 120, 18, ST77XX_WHITE);
}

void drawEHLive() {
  tft.fillRect(20, 100, 125, 55, ST77XX_BLACK);
  tft.fillRect(180, 100, 125, 55, ST77XX_BLACK);

  tft.setTextSize(3);
  tft.setTextColor(levelColor(ePercent), ST77XX_BLACK);
  tft.setCursor(35, 105);
  tft.print(ePercent);
  tft.print("%");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(25, 145);
  tft.print("SIG:");
  tft.print(signal_mV, 1);
  tft.print("mV");

  tft.setTextSize(3);
  tft.setTextColor(levelColor(hPercent), ST77XX_BLACK);
  tft.setCursor(195, 105);
  tft.print(hPercent);
  tft.print("%");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(185, 145);
  tft.print("MAG:");
  tft.print(magTotal, 1);
  tft.print("uT");

  tft.fillRect(20, 190, 285, 15, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(25, 192);
  tft.print("DIST:");
  if (tofValid) {
    tft.print(distance_cm, 1);
    tft.print("cm");
  } else {
    tft.print("--");
  }

  int eBar = map(ePercent, 0, 100, 0, 116);
  int hBar = map(hPercent, 0, 100, 0, 116);

  tft.fillRect(22, 167, 116, 14, ST77XX_BLACK);
  tft.fillRect(182, 167, 116, 14, ST77XX_BLACK);

  tft.fillRect(22, 167, eBar, 14, levelColor(ePercent));
  tft.fillRect(182, 167, hBar, 14, levelColor(hPercent));

  tft.drawRect(20, 165, 120, 18, ST77XX_WHITE);
  tft.drawRect(180, 165, 120, 18, ST77XX_WHITE);
}

// ===============================
// MODO 1: EMI RELATIVO
// ===============================
void drawEMIBase() {
  drawBaseUI("LOCALIZACION RELATIVA EMI");

  tft.drawRect(25, 80, 270, 30, ST77XX_WHITE);
  tft.drawRect(25, 170, 270, 20, ST77XX_WHITE);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(35, 200);
  tft.print("Indice = E normalizado + H normalizado");
}

void drawEMILive() {
  tft.fillRect(40, 120, 240, 45, ST77XX_BLACK);

  tft.setTextSize(4);
  tft.setTextColor(levelColor(emiPercent), ST77XX_BLACK);
  tft.setCursor(75, 120);
  tft.print("EMI ");
  tft.print(emiPercent);
  tft.print("%");

  int bar = map(emiPercent, 0, 100, 0, 266);

  tft.fillRect(27, 82, 266, 26, ST77XX_BLACK);
  tft.fillRect(27, 82, bar, 26, levelColor(emiPercent));
  tft.drawRect(25, 80, 270, 30, ST77XX_WHITE);

  tft.fillRect(30, 172, 260, 16, ST77XX_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(35, 175);
  tft.print("E:");
  tft.print(ePercent);
  tft.print("%  H:");
  tft.print(hPercent);
  tft.print("%  D:");
  if (tofValid) {
    tft.print(distance_cm, 0);
    tft.print("cm");
  } else {
    tft.print("--");
  }
}

// ===============================
// MODO 2: WIFI / MATLAB
// ===============================
void drawWiFiBase() {
  drawBaseUI("WIFI ACTIVO / MATLAB CONNECTION");

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(25, 75);
  tft.print("WIFI EMI SCAN CONNECTION");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(25, 115);
  //tft.print("RED: ");
  tft.print(ssid);

  tft.setCursor(25, 135);
  //tft.print("PASS: ");
  tft.print(password);

  tft.setCursor(25, 155);
  tft.print("IP: 192.168.4.1");

  tft.setCursor(25, 175);
  tft.print("MATLAB URL:");

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(25, 190);
  tft.print("http://192.168.4.1/data");
}

void drawWiFiLive() {
  tft.fillRect(20, 225, 285, 35, ST77XX_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(25, 225);
  tft.print("Sensando: E ");
  tft.print(ePercent);
  tft.print("% | H ");
  tft.print(hPercent);
  tft.print("% | EMI ");
  tft.print(emiPercent);
  tft.print("% | D ");
  if (tofValid) {
    tft.print(distance_cm, 0);
    tft.print("cm");
  } else {
    tft.print("--");
  }
}

// ===============================
// DRAW GENERAL
// ===============================
void drawValues() {
  if (screenMode != lastScreenMode) {
    if (screenMode == 0) drawEHBase();
    if (screenMode == 1) drawEMIBase();
    if (screenMode == 2) drawWiFiBase();

    lastScreenMode = screenMode;
  }

  if (screenMode == 0) drawEHLive();
  if (screenMode == 1) drawEMILive();
  if (screenMode == 2) drawWiFiLive();
}

// ===============================
// BOTON
// ===============================
void updateButton() {
  bool pressed = digitalRead(BTN_CAL) == LOW;

  if (pressed && !buttonWasPressed) {
    buttonWasPressed = true;
    buttonPressStart = millis();
  }

  if (!pressed && buttonWasPressed) {
    unsigned long pressTime = millis() - buttonPressStart;
    buttonWasPressed = false;

    if (millis() - lastButton > 250) {
      lastButton = millis();

      if (pressTime > 1200) {
        calibrateBaseline();
      } else {
        screenMode++;
        if (screenMode > 2) screenMode = 0;
        lastScreenMode = -1;
      }
    }
  }
}

// ===============================
// WEB SERVER MATLAB
// ===============================
void handleData() {
  String json = "{";

  json += "\"raw_mV\":" + String(raw_mV, 3) + ",";
  json += "\"filtered_mV\":" + String(filtered_mV, 3) + ",";
  json += "\"baseline_mV\":" + String(baseline_mV, 3) + ",";
  json += "\"signal_mV\":" + String(signal_mV, 3) + ",";
  json += "\"ePercent\":" + String(ePercent) + ",";
  json += "\"hPercent\":" + String(hPercent) + ",";
  json += "\"emiPercent\":" + String(emiPercent) + ",";
  json += "\"noiseFloor_mV\":" + String(noiseFloor_mV, 3) + ",";
  json += "\"maxSignal_mV\":" + String(maxSignal_mV, 3) + ",";
  json += "\"saturated\":" + String(adcSaturated ? 1 : 0) + ",";
  json += "\"distance_mm\":" + String(distance_mm) + ",";
  json += "\"distance_cm\":" + String(distance_cm, 2) + ",";
  json += "\"tof_valid\":" + String(tofValid ? 1 : 0) + ",";

  json += "\"mag_x\":" + String(mx, 3) + ",";
  json += "\"mag_y\":" + String(my, 3) + ",";
  json += "\"mag_z\":" + String(mz, 3) + ",";
  json += "\"mag_total\":" + String(magTotal, 3) + ",";
  json += "\"mag_baseline\":" + String(magBaseline, 3) + ",";
  json += "\"hSignal\":" + String(hSignal, 3) + ",";

  json += "\"acc_x\":" + String(ax, 3) + ",";
  json += "\"acc_y\":" + String(ay, 3) + ",";
  json += "\"acc_z\":" + String(az, 3) + ",";

  json += "\"gyro_x\":" + String(gx, 3) + ",";
  json += "\"gyro_y\":" + String(gy, 3) + ",";
  json += "\"gyro_z\":" + String(gz, 3);

  json += "}";

  server.send(200, "application/json", json);
}

void handleCalibrate() {
  calibrateBaseline();
  server.send(200, "text/plain", "OK CALIBRATED");
}

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");

  if (!file) {
    server.send(500, "text/plain", "ERROR: /index.html no encontrado en LittleFS. Usa PlatformIO: Upload Filesystem Image.");
    return;
  }

  server.streamFile(file, "text/html");
  file.close();
}

// ===============================
// SETUP
// ===============================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BTN_CAL, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.init(240, 320);
  tft.setRotation(1);
 //  uint16_t time = millis();
  tft.fillScreen(ST77XX_BLACK);
 // time = millis() - time;

  introAnimation();
  //delay(1000);

  if (!LittleFS.begin(true)) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(2);
    tft.setCursor(20, 90);
    tft.print("ERROR LittleFS");
    delay(1500);
  }

  startWiFiAP();

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 70);
  tft.print("EMI-SCAN");

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(20, 105);
  tft.print("Iniciando ADS1115 + MPU9250");

  if (!ads.begin(0x48)) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(2);
    tft.setCursor(20, 90);
    tft.print("ERROR ADS1115");
    while (1);
  }
// ===============================
// MPU9250
// ===============================
mpu.setWire(&Wire);

mpu.beginAccel();
mpu.beginGyro();
mpu.beginMag();

delay(300);

// Prueba simple para confirmar que responde
mpu.accelUpdate();
mpu.gyroUpdate();
mpu.magUpdate();

if (isnan(mpu.accelX()) || isnan(mpu.gyroX()) || isnan(mpu.magX())) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(2);
  tft.setCursor(20, 90);
  tft.print("ERROR MPU9250");

  while (1);
}

// ===============================
// VL53L4CD / ToF
// ===============================
if (tof.begin() != 0) {
  tofOK = false;

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(2);
  tft.setCursor(20, 90);
  tft.print("ERROR VL53L4CD");

  tft.setTextSize(1);
  tft.setCursor(20, 125);
  tft.print("Revisa I2C SDA/SCL y VCC");

  while (1);
}

tof.VL53L4CD_Off();
tof.InitSensor();
tof.VL53L4CD_SetRangeTiming(50, 0);
tof.VL53L4CD_StartRanging();

tofOK = true;

  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_860SPS);

  disableWiFi();

  calibrateBaseline();

//  Serial.println("===== EMI-SCAN READY =====");
//  Serial.println("Modo 0: E/H");
//  Serial.println("Modo 1: EMI relativo");
//  Serial.println("Modo 2: MATLAB/WiFi");
//  Serial.println("==========================");
}

// ===============================
// LOOP
// ===============================
void loop() {
  updateButton();

  if (screenMode == 2) {
    enableWiFi();
    server.handleClient();
  } else {
    disableWiFi();
  }

  if (millis() - lastSample >= 5) {
    updateMPU();
    updateToF();
    updateEField();
    updateHField();
    updateEMIIndex();

    lastSample = millis();
  }

  if (millis() - lastDraw >= 35) {
    drawValues();
    lastDraw = millis();
  }
}