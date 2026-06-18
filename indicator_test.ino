#include <SPI.h>
#include <math.h>
#include <TFT_eSPI.h>
#include "eurostile_extd_black_italic8pt7b.h"

#pragma GCC optimize ("Ofast")

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);
TFT_eSprite needle = TFT_eSprite(&tft);

// --- ピン・設定定数 ---
#define TFT_BL 16
#define CENTER_X 114 
#define CENTER_Y 160 
#define CENTER_X2 356
#define CENTER_Y2 160
#define RID_SIZE 230
#define NEEDLE_WIDTH 4
#define NEEDLE_LENGTH 94 
#define BUZZER_PIN 20 

const int SENSOR_WATER = 26; 
const int SENSOR_OIL   = 27; 
const int SW_PIN = 28;

// --- 色の定義 ---
#define NEEDLE_COL     tft.color565(255, 255, 255)
#define GAUGE_TEXT_COL tft.color565(200, 200, 200)
#define GAUGE_EDGE_COL tft.color565(140, 170, 240)       // 通常時の青
#define GAUGE_EDGE_START_COL tft.color565(0, 0, 250) // アニメ中の遷移色（白）
#define REDZONE_COL    tft.color565(90, 90, 220)    
#define WARN_FLASH_COL tft.color565(255, 0, 0)     
#define EDGE_1         tft.color565(80, 80, 80)
#define EDGE_2         tft.color565(50, 50, 50)
#define M_BLACK        TFT_BLACK

// --- レッドゾーン開始温度 ---
#define WATER_REDZONE_TEMP 115.0
#define OIL_REDZONE_TEMP   115.0
#define MAX_GAUGE_TEMP     130.0 

// --- 平滑化フィルタ設定 ---
const float FILTER_BETA = 0.05; 

struct LabelOffset {
    int x;
    int y;
};

LabelOffset offsets[] = {
    {  0,  0}, {  0,  0}, {  0,  0}, { -3,  0}, { -3,  0}, 
    {  0,  0}, { -5,  0}, {  0,  0}, {  0,  3}, {  0,  0}, {  0, -3}
};

// --- 共有変数 ---
volatile float currentAngle1 = 90.0;
volatile float currentAngle2 = 90.0;
volatile float currentTemp1 = 20.0; 
volatile float currentTemp2 = 20.0; 
volatile bool isRunning = false;
volatile bool showNeedle = true;
volatile float bgBrightness = 0.0; 
volatile uint16_t dynamicNeedleColor = M_BLACK;
volatile uint16_t dynamicEdgeColor = M_BLACK; 

// ★改善版：2つの色を安全に混ぜる（負の遷移に対応）
uint16_t lerpColor(uint16_t c1, uint16_t c2, float ratio) {
    if (ratio <= 0.0) return c1;
    if (ratio >= 1.0) return c2;
    
    int16_t r1 = (c1 >> 11) & 0x1F;
    int16_t g1 = (c1 >> 5) & 0x3F;
    int16_t b1 = c1 & 0x1F;
    
    int16_t r2 = (c2 >> 11) & 0x1F;
    int16_t g2 = (c2 >> 5) & 0x3F;
    int16_t b2 = c2 & 0x1F;
    
    // int16_t で計算することで (r2 - r1) が負になっても正しく補間できる
    uint16_t r = r1 + (int16_t)((float)(r2 - r1) * ratio);
    uint16_t g = g1 + (int16_t)((float)(g2 - g1) * ratio);
    uint16_t b = b1 + (int16_t)((float)(b2 - b1) * ratio);
    
    return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t fadeColor(uint16_t color, float ratio) {
    if (ratio <= 0.0) return M_BLACK;
    if (ratio >= 1.0) return color;
    uint8_t r = (uint8_t)(((color >> 11) & 0x1F) * ratio);
    uint8_t g = (uint8_t)(((color >> 5) & 0x3F) * ratio);
    uint8_t b = (uint8_t)((color & 0x1F) * ratio);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// --- 背景描画エンジン ---
void drawGaugeBackground(TFT_eSprite* s, int cx, int cy, const char* title, float ratio, float redStartTemp, float currentTemp, bool blinkState) {
    s->fillSprite(M_BLACK);
    
    bool isWarning = (ratio >= 1.0 && currentTemp >= redStartTemp);
    uint16_t currentEdgeCol = isWarning ? (blinkState ? WARN_FLASH_COL : GAUGE_EDGE_COL) : dynamicEdgeColor;

    for(int r = 108; r <= 111; r++) { s->drawCircle(cx, cy, r, currentEdgeCol); }

    if (ratio > 0.01) {
        s->drawCircle(cx, cy, 102, EDGE_1);
        uint16_t dynamicTextCol = GAUGE_TEXT_COL;
        uint16_t dynamicRedCol  = REDZONE_COL;

        float redStartAngle = 90.0 + (redStartTemp - 30.0) * 2.7;
        for (float a = redStartAngle; a <= 360.0; a += 0.5) {
            float rad = a * M_PI / 180.0;
            s->drawLine(cx + cos(rad) * 90, cy + sin(rad) * 90, cx + cos(rad) * 102, cy + sin(rad) * 102, dynamicRedCol);
        }

        for (int angle = 90; angle <= 360; angle += 5.4) {
            int step = round((angle - 90.0) / 5.4);
            int inner = (step % 5 == 0) ? 88 : 94;
            uint16_t tickCol = (angle >= redStartAngle) ? M_BLACK : dynamicTextCol;
            for (float offset = -0.4; offset <= 0.4; offset += 0.2) {
                float rad_bold = (angle + offset) * M_PI / 180.0;
                s->drawLine(cx + cos(rad_bold) * inner, cy + sin(rad_bold) * inner, cx + cos(rad_bold) * 100, cy + sin(rad_bold) * 100, tickCol);
            }
        }

        s->setTextColor(dynamicTextCol); s->setTextDatum(MC_DATUM); s->setFreeFont(&eurostile_extd_black_italic8pt7b);
        if (strcmp(title, "OIL TEMP") == 0) {
            s->drawString("OIL", cx + 70, cy + 20); s->drawString("TEMP", cx + 60, cy + 35); 
        } else if (strcmp(title, "WATER TEMP") == 0) {
            s->drawString("WATER", cx + 53, cy + 20); s->drawString("TEMP", cx + 58, cy + 35);
        }
        
        int values[] = {30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130};
        int angles[] = {90, 117, 144, 171, 198, 225, 252, 279, 306, 333, 360};
        for (int i = 0; i < 11; i++) {
            float rad = angles[i] * M_PI / 180.0;
            s->drawNumber(values[i], (cx + cos(rad) * 68) + offsets[i].x, (cy + sin(rad) * 78) + offsets[i].y);
        }
    }
    s->setFreeFont(NULL);
}

void updateSensorData(int pin, volatile float &outAngle, volatile float &outTemp) {
    int rawADC = analogRead(pin);
    float Vout = (float)(rawADC < 1 ? 1 : rawADC) * 3.3 / 4095.0;
    float Rs = 10000.0 * Vout / (3.3 - Vout + 0.0001); 
    float rawTemp = 1.0 / (log(Rs / 11000.0) / 3500.0 + 1.0 / (20.0 + 273.15)) - 273.15;
    if (outTemp < -50) outTemp = rawTemp; 
    outTemp = outTemp + FILTER_BETA * (rawTemp - outTemp); 
    float targetAngle = 90.0 + (outTemp - 30.0) * 2.7;
    outAngle = outAngle + FILTER_BETA * (constrain(targetAngle, 90.0, 360.0) - outAngle);
}

void setup() {
    Serial.begin(115200);
    pinMode(BUZZER_PIN, OUTPUT);
    gpio_set_drive_strength(BUZZER_PIN, GPIO_DRIVE_STRENGTH_12MA);
    analogReadResolution(12);
}

void loop() {
    static unsigned long startTime = millis();
    unsigned long elapsedTime = millis() - startTime;

    if (elapsedTime < 4400) {
        if (elapsedTime <= 400 || elapsedTime >= 3825) {
            showNeedle = false; bgBrightness = 0;
            dynamicEdgeColor = (elapsedTime >= 3825) ? GAUGE_EDGE_COL : M_BLACK;
        } else {
            showNeedle = true;
            float ang = 90.0, needleFade = 1.0;
            bgBrightness = 0; 

            if (elapsedTime < 1600) {
                needleFade = (float)(elapsedTime - 400) / 1200.0;
                dynamicEdgeColor = fadeColor(GAUGE_EDGE_COL, needleFade);
            } else if (elapsedTime < 2525) {
                // 1600-2525ms: 青 -> 白
                float progress = (float)(elapsedTime - 1600) / 925.0;
                dynamicEdgeColor = lerpColor(GAUGE_EDGE_COL, GAUGE_EDGE_START_COL, progress);
                ang = 90.0 + ((1.0 - cos(progress * M_PI)) / 2.0 * 270.0);
            } else if (elapsedTime < 2900) {
                // 2525-2900ms: 白保持
                dynamicEdgeColor = GAUGE_EDGE_START_COL;
                ang = 360.0;
            } else if (elapsedTime < 3825) {
                // 2900-3825ms: 白 -> 青（★ここが正しく動くようになります）
                float progress = (float)(elapsedTime - 2900) / 925.0;
                dynamicEdgeColor = lerpColor(GAUGE_EDGE_START_COL, GAUGE_EDGE_COL, progress);
                ang = 360.0 - ((1.0 - cos(progress * M_PI)) / 2.0 * 270.0);
            }

            dynamicNeedleColor = fadeColor(NEEDLE_COL, needleFade);
            currentAngle1 = ang; currentAngle2 = ang;
        }
    } else {
        showNeedle = true; isRunning = true; bgBrightness = 1.0; 
        dynamicNeedleColor = NEEDLE_COL;
        dynamicEdgeColor = GAUGE_EDGE_COL;
        updateSensorData(SENSOR_WATER, currentAngle1, currentTemp1);
        updateSensorData(SENSOR_OIL,   currentAngle2, currentTemp2);

        bool waterCrit = (currentTemp1 >= MAX_GAUGE_TEMP);
        bool oilCrit   = (currentTemp2 >= MAX_GAUGE_TEMP);
        bool waterWarn = (currentTemp1 >= WATER_REDZONE_TEMP);
        bool oilWarn   = (currentTemp2 >= OIL_REDZONE_TEMP);
        bool blink     = (millis() % 500) < 250;

        if (waterCrit || oilCrit) tone(BUZZER_PIN, 2000);
        else if ((waterWarn || oilWarn) && blink) tone(BUZZER_PIN, 1500);
        else noTone(BUZZER_PIN);
    }
    delay(10);
}

void setup1() {
    tft.begin(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
    canvas.createSprite(RID_SIZE, RID_SIZE); canvas.setPivot(RID_SIZE / 2, RID_SIZE / 2);
    needle.createSprite(NEEDLE_LENGTH, NEEDLE_WIDTH); needle.setPivot(0, NEEDLE_WIDTH / 2);
    pinMode(TFT_BL, OUTPUT); analogWrite(TFT_BL, 255);
}

void loop1() {
    bool isWarnBlink = (millis() % 500) < 250;
    needle.fillSprite(TFT_BLACK);
    needle.fillRect(0, 0, NEEDLE_LENGTH, NEEDLE_WIDTH, dynamicNeedleColor);
    tft.startWrite();
    drawGaugeBackground(&canvas, RID_SIZE/2, RID_SIZE/2, "WATER TEMP", bgBrightness, WATER_REDZONE_TEMP, currentTemp1, isWarnBlink);
    if (showNeedle) needle.pushRotated(&canvas, (int)currentAngle1, TFT_BLACK); 
    canvas.pushSprite(CENTER_X - RID_SIZE/2, CENTER_Y - RID_SIZE/2);
    drawGaugeBackground(&canvas, RID_SIZE/2, RID_SIZE/2, "OIL TEMP", bgBrightness, OIL_REDZONE_TEMP, currentTemp2, isWarnBlink);
    if (showNeedle) needle.pushRotated(&canvas, (int)currentAngle2, TFT_BLACK); 
    canvas.pushSprite(CENTER_X2 - RID_SIZE/2, CENTER_Y2 - RID_SIZE/2);
    tft.endWrite();
    delay(2);
}