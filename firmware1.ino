#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ---------------- Pins ----------------
const uint8_t PIN_SDA    = 4;
const uint8_t PIN_SCL    = 5;
const uint8_t PIN_ENC_A  = 0;   // encoder CLK
const uint8_t PIN_ENC_B  = 1;   // encoder DT
const uint8_t PIN_FLIP   = 8;   // flip (slide) switch
// Buttons: 0 = red, 1 = green, 2 = blue, 3 = encoder click
const uint8_t PIN_BTN[4] = {6, 7, 9, 3};
const uint8_t PIN_LED[3] = {18, 19, 2};  // red, green, blue

const char* COLOR_NAME[3] = {"RED", "GREEN", "BLUE"};

// If you must turn the encoder twice per step in Wokwi, change 4 to 2
const int ENC_TICKS_PER_STEP = 4;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---------------- Rotary encoder ----------------
volatile int32_t encCount = 0;
volatile uint8_t encPrev  = 0;
static const DRAM_ATTR int8_t ENC_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void IRAM_ATTR encISR() {
  encPrev = ((encPrev << 2) | (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B)) & 0x0F;
  encCount += ENC_TABLE[encPrev];
}

int readEncSteps() {
  int result = 0;
  noInterrupts();
  if (encCount >= ENC_TICKS_PER_STEP)       { encCount -= ENC_TICKS_PER_STEP; result = 1; }
  else if (encCount <= -ENC_TICKS_PER_STEP) { encCount += ENC_TICKS_PER_STEP; result = -1; }
  interrupts();
  return result;
}

// ---------------- Buttons ----------------
bool     btnStable[4], btnRaw[4], btnEdge[4];
uint32_t btnTime[4];

void updateButtons() {
  uint32_t now = millis();
  for (int i = 0; i < 4; i++) {
    bool raw = (digitalRead(PIN_BTN[i]) == LOW);
    if (raw != btnRaw[i]) { btnRaw[i] = raw; btnTime[i] = now; }
    if ((now - btnTime[i]) > 20 && raw != btnStable[i]) {
      btnStable[i] = raw;
      if (raw) btnEdge[i] = true;
    }
  }
}

bool pressed(int i) {
  bool e = btnEdge[i];
  btnEdge[i] = false;
  return e;
}

void clearInputs() {
  for (int i = 0; i < 4; i++) btnEdge[i] = false;
  noInterrupts(); encCount = 0; interrupts();
}

// ---------------- LEDs ----------------
void setLeds(int which) {   // 0..2 = one LED, -1 = all off, 3 = all on
  for (int i = 0; i < 3; i++) {
    bool on = (which == 3) || (which == i);
    digitalWrite(PIN_LED[i], on ? HIGH : LOW);
  }
}

// ---------------- Drawing helpers ----------------
void drawCentered(const char* text, int y) {
  int w = u8g2.getStrWidth(text);
  u8g2.drawStr((128 - w) / 2, y, text);
}

void showGameOver(const char* title, int score, int best) {
  setLeds(-1);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  drawCentered("GAME OVER", 14);
  u8g2.setFont(u8g2_font_6x12_tr);
  char buf[24];
  snprintf(buf, sizeof(buf), "%s score: %d", title, score);
  drawCentered(buf, 32);
  snprintf(buf, sizeof(buf), "Best: %d", best);
  drawCentered(buf, 46);
  drawCentered("Click to continue", 60);
  u8g2.sendBuffer();

  delay(400);
  clearInputs();
  while (true) {
    updateButtons();
    if (pressed(3)) break;
    delay(5);
  }
  clearInputs();
}

// ---------------- Game 1: BIP IT ----------------
int bestBip = 0;

void playBip() {
  int score = 0;
  uint32_t timeLimit = 3000;
  bool flipLast = (digitalRead(PIN_FLIP) == LOW);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  drawCentered("GET READY", 36);
  u8g2.sendBuffer();
  setLeds(3);
  delay(900);
  setLeds(-1);
  clearInputs();

  while (true) {
    int cmd = random(5);   // 0-2 colours, 3 flip, 4 twist
    char label[16];
    if (cmd < 3)       snprintf(label, sizeof(label), "%s IT!", COLOR_NAME[cmd]);
    else if (cmd == 3) snprintf(label, sizeof(label), "FLIP IT!");
    else               snprintf(label, sizeof(label), "TWIST IT!");

    setLeds(cmd < 3 ? cmd : -1);
    clearInputs();

    uint32_t start = millis();
    uint32_t lastDraw = 0;
    bool ok = false, fail = false;

    while (!ok && !fail) {
      uint32_t now = millis();
      uint32_t elapsed = now - start;
      if (elapsed >= timeLimit) { fail = true; break; }

      updateButtons();
      for (int i = 0; i < 3; i++) {
        if (pressed(i)) { if (cmd == i) ok = true; else fail = true; }
      }
      pressed(3);

      if (readEncSteps() != 0) { if (cmd == 4) ok = true; else fail = true; }

      bool flipNow = (digitalRead(PIN_FLIP) == LOW);
      if (flipNow != flipLast) {
        flipLast = flipNow;
        if (cmd == 3) ok = true; else fail = true;
      }

      if (now - lastDraw > 40) {
        lastDraw = now;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x12_tr);
        char sc[16];
        snprintf(sc, sizeof(sc), "Score %d", score);
        u8g2.drawStr(0, 10, sc);
        u8g2.setFont(u8g2_font_ncenB10_tr);
        drawCentered(label, 38);
        int barW = (int)(126UL * (timeLimit - elapsed) / timeLimit);
        u8g2.drawFrame(0, 52, 128, 10);
        u8g2.drawBox(1, 53, barW, 8);
        u8g2.sendBuffer();
      }
    }

    if (!ok) break;

    score++;
    timeLimit = (score * 80 >= 2200) ? 800 : (3000 - score * 80);
    setLeds(-1);
    delay(120);
  }

  if (score > bestBip) bestBip = score;
  showGameOver("Bip It", score, bestBip);
}

// ---------------- Game 2: DINO RUN ----------------
int bestDino = 0;

void drawDino(int x, int groundY, float h, bool legFrame) {
  int top = groundY - 12 - (int)h;
  u8g2.drawBox(x, top + 2, 8, 8);
  u8g2.drawBox(x + 4, top, 6, 5);
  u8g2.setDrawColor(0);
  u8g2.drawPixel(x + 8, top + 1);
  u8g2.setDrawColor(1);
  if (h > 0.5f || legFrame) {
    u8g2.drawVLine(x + 2, top + 10, 2);
    u8g2.drawVLine(x + 6, top + 10, 1);
  } else {
    u8g2.drawVLine(x + 2, top + 10, 1);
    u8g2.drawVLine(x + 6, top + 10, 2);
  }
}

void drawCactus(int x, int groundY, int w, int h) {
  u8g2.drawBox(x + w / 2 - 1, groundY - h, 3, h);
  u8g2.drawBox(x, groundY - h + 3, w / 2, 2);
  u8g2.drawBox(x + w / 2 + 2, groundY - h + 5, w / 2, 2);
}

void playDino() {
  const int GROUND = 54;
  const int DINO_X = 10;
  float h = 0, vy = 0;
  float cx = 140; int cw = 7, ch = 12;
  float speed = 2.5f;
  uint32_t frame = 0;
  int score = 0;

  clearInputs();
  uint32_t lastFrame = millis();

  while (true) {
    uint32_t now = millis();
    if (now - lastFrame < 30) { updateButtons(); continue; }
    lastFrame = now;
    frame++;

    updateButtons();
    bool jump = pressed(0) | pressed(1) | pressed(2) | pressed(3);
    readEncSteps();
    if (jump && h <= 0.01f) vy = 5.5f;

    h += vy;
    vy -= 0.6f;
    if (h < 0) { h = 0; vy = 0; }

    cx -= speed;
    if (cx < -cw) {
      cx = 128 + random(10, 70);
      cw = random(5, 9);
      ch = random(9, 15);
    }

    score = frame / 4;
    speed = 2.5f + min(2.5f, score / 200.0f);

    int dinoTop = GROUND - 12 - (int)h;
    int dinoBottom = GROUND - (int)h;
    bool hitX = (DINO_X + 9 > (int)cx + 1) && (DINO_X + 1 < (int)cx + cw);
    bool hitY = (dinoBottom > GROUND - ch + 1) && (dinoTop < GROUND);
    if (hitX && hitY) break;

    u8g2.clearBuffer();
    u8g2.drawHLine(0, GROUND, 128);
    for (int i = 0; i < 5; i++) {
      int gx = (int)(128 - ((frame * (int)speed + i * 29) % 128));
      u8g2.drawPixel(gx, GROUND + 3 + (i % 3) * 2);
    }
    drawDino(DINO_X, GROUND, h, (frame / 4) % 2);
    drawCactus((int)cx, GROUND, cw, ch);
    u8g2.setFont(u8g2_font_6x12_tr);
    char sc[16];
    snprintf(sc, sizeof(sc), "%05d", score);
    u8g2.drawStr(92, 10, sc);
    u8g2.sendBuffer();
  }

  if (score > bestDino) bestDino = score;
  showGameOver("Dino", score, bestDino);
}

// ---------------- Menu ----------------
void runMenu() {
  const char* items[2] = {"BIP IT", "DINO RUN"};
  int sel = 0;
  clearInputs();

  while (true) {
    updateButtons();
    int s = readEncSteps();
    if (s != 0) sel = (sel + s + 2) % 2;
    if (pressed(3)) break;
    pressed(0); pressed(1); pressed(2);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB14_tr);
    drawCentered("BIP IT", 18);
    u8g2.setFont(u8g2_font_7x13B_tr);
    for (int i = 0; i < 2; i++) {
      int y = 40 + i * 16;
      if (i == sel) {
        u8g2.drawBox(20, y - 11, 88, 14);
        u8g2.setDrawColor(0);
        drawCentered(items[i], y);
        u8g2.setDrawColor(1);
      } else {
        drawCentered(items[i], y);
      }
    }
    u8g2.sendBuffer();
    delay(20);
  }

  if (sel == 0) playBip();
  else          playDino();
}

// ---------------- Setup / loop ----------------
void setup() {
  for (int i = 0; i < 4; i++) pinMode(PIN_BTN[i], INPUT_PULLUP);
  for (int i = 0; i < 3; i++) { pinMode(PIN_LED[i], OUTPUT); digitalWrite(PIN_LED[i], LOW); }
  pinMode(PIN_FLIP, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);

  encPrev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  Wire.setPins(PIN_SDA, PIN_SCL);
  u8g2.begin();
  u8g2.setBusClock(400000);

  randomSeed(micros() ^ analogRead(PIN_BTN[0]));

  setLeds(3); delay(300); setLeds(-1);
}

void loop() {
  runMenu();
}
