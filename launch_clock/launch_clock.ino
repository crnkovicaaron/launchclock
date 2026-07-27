#include <Wire.h>
#include <RTClib.h>
#include <Keypad.h>
#include "LedControl.h"

RTC_DS1307 rtc;

// L-0 time. Format: {Y, M, D, h, m, s}
long L_Zero[6] = {2026, 4, 22, 0, 0, 0};
uint32_t launchTime;

const unsigned long autoCancel = 10; // time mode autocancel (in seconds)
bool hourMode = false; // Start in day mode
bool displayOn = true; // Start with display on
bool pauseActive = false; 
bool timeModeActive = false;
bool exitPause = false;
bool exitTimeMode = false;
bool LD_Reset = false;
bool LT_Reset = false;
const uint8_t* lastMatrix = nullptr;

const uint8_t rowPins[4] = {53, 52, 51, 50};
const uint8_t colPins[4] = {49, 48, 47, 46};
const int matrixPins[3] = {A8, A9, A10};

const uint8_t digitPins[2][4] = {
  {42, 43, 44, 45},
  {38, 39, 40, 41}
};
const uint8_t segmentPins[2][8] = { // order: a, b, c, d, e, f, g, DP
  {30, 31, 32, 33, 34, 35, 36, 37},
  {22, 23, 24, 25, 26, 27, 28, 29}
};
const char keys[4][4] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
const uint8_t L_Plus[8] = {
  0b00001000,
  0b00011100,
  0b00001000,
  0b00000000,
  0b00000001,
  0b00000001,
  0b00000001,
  0b11111111
};
const uint8_t L_Minus[8] = {
  0b00001000,
  0b00001000,
  0b00001000,
  0b00000000,
  0b00000001,
  0b00000001,
  0b00000001,
  0b11111111
};
const uint8_t L_Pause[8] = {
  0b00011100,
  0b00000000,
  0b00011100,
  0b00000000,
  0b00000001,
  0b00000001,
  0b00000001,
  0b11111111
};
const uint8_t timeMatrix[8] = {
  0b10000000,
  0b11111111,
  0b10000000,
  0b00000000,
  0b00000000,
  0b10000001,
  0b10000001,
  0b11111111
};
const uint8_t LT_Matrix[8] = {
  0b10000000,
  0b10000000,
  0b11111111,
  0b10000000,
  0b10000001,
  0b00000001,
  0b00000001,
  0b11111111
};
const uint8_t LD_Matrix[8] = {
  0b01111110,
  0b10000001,
  0b10000001,
  0b11111111,
  0b00000000,
  0b00000001,
  0b00000001,
  0b11111111
};
const uint8_t matrixOFF[8] = {
  0b00000000,
  0b00000000,
  0b00000000,
  0b00000000,
  0b00000000,
  0b00000000,
  0b00000000,
  0b00000000
};
uint8_t segmentBytes[2][11][2];
const uint8_t segmentPatterns[11][8] = {
  {1,1,1,1,1,1,0,0}, // 0
  {0,1,1,0,0,0,0,0}, // 1
  {1,1,0,1,1,0,1,0}, // 2
  {1,1,1,1,0,0,1,0}, // 3
  {0,1,1,0,0,1,1,0}, // 4
  {1,0,1,1,0,1,1,0}, // 5
  {1,0,1,1,1,1,1,0}, // 6
  {1,1,1,0,0,0,0,0}, // 7
  {1,1,1,1,1,1,1,0}, // 8
  {1,1,1,0,0,1,1,0}, // 9
  {0,0,0,0,0,0,0,0}  // 10 = blank
};
uint8_t dispVals[2][4] = {
  {0, 0, 0, 0},
  {0, 0, 0, 0}
};
bool dispDP[2][4]  = {
  {false, true, false, true},
  {false, true, false, false}
};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);
LedControl matrixDisplay = LedControl(matrixPins[0], matrixPins[2], matrixPins[1]);
unsigned long lastUpdate = 0;


uint32_t toUnix(long T[]) {
  DateTime launch(T[0], T[1], T[2], T[3], T[4], T[5]);
  uint32_t t = launch.unixtime();
  return(t);
}
void initSegmentBytes() {
  for (uint8_t i = 0; i < 2; i++) {
    for (uint8_t n = 0; n < 11; n++) {
      for (uint8_t dp = 0; dp < 2; dp++) {
        uint8_t val = 0;
        for (uint8_t s = 0; s < 8; s++) {
          uint8_t bit = segmentPatterns[n][s];
          if (s == 7 && dp) bit = 1;               // DP override
          if (bit) {
            if (i == 0) val |= (1 << (7 - s));     // PORTC (reversed)
            else        val |= (1 << s);           // PORTA (normal)
          }
        }
        segmentBytes[i][n][dp] = val;
      }
    }
  }
}

void keypadEvent(KeypadEvent key) {
  if (LT_Reset || LD_Reset) return;
  switch (keypad.getState()) {
    case PRESSED:
      if (key == 'D' && pauseActive) exitPause = true;
      break;
    
    case HOLD:
      if (key == 'D' && !pauseActive) {
        launchTime = toUnix(L_Zero);
        exitPause = true;
      }
      if (key == '#') resetLaunchTime();
      if (key == '*') resetLaunchDate();
      break;
    
    case RELEASED:
      if (key == 'A') {
        displayOn = !displayOn;
        findMode();
      }
      if (key == 'B') {
        hourMode = !hourMode;
        findMode();
      }
      if (key == 'C') {
        if (!timeModeActive) {
          showTime();
        } else {
          exitTimeMode = true;
        }
      }
      if (key == 'D') {
        if (!pauseActive && !exitPause) pauseCountdown();
        else {
          exitPause = false;
          pauseActive = false;
        }
      }
      break;
    default:
      break;
  }
}

void resetLaunchDate() {
  LD_Reset = true;
  displayMatrix(LD_Matrix);
  for (int i = 0; i < 4; i++) {
    dispVals[0][i] = 10;
    dispVals[1][i] = 10;
    dispDP[0][i] = false;
    dispDP[1][i] = false;
  }
  dispDP[0][3] = true;
  dispDP[1][1] = true;
  unsigned long lastBlink = millis();
  const unsigned long blinkInterval = 500;
  bool matrixOn = true;

  String inpString = "";
  bool inputDone = false;

  while (!inputDone) {
    char key = keypad.getKey();
    if (key) {
      switch (key) {
        case '0' ... '9':
          if (inpString.length() < 8) {
            inpString += key;
            showInputPreview(inpString);
          }
          break;
          
        case '*':
          if (inpString.length() == 8) {
            parseAndSetLaunchTime(inpString);
            inputDone = true;
          } else {
            LD_Reset = false;
            return;
          }
          break;
        
        case 'D':
          if (inpString.length() > 0) {
            inpString.remove(inpString.length() - 1);
            showInputPreview(inpString);
          }
          break;
      }
    }

    if (millis() - lastBlink >= blinkInterval) {
      matrixOn = !matrixOn;
      lastBlink = millis();

      if (matrixOn) {
        displayMatrix(LD_Matrix);
      } else {
        displayMatrix(matrixOFF);
      }
    }

    refreshDisplays();
    delay(1);
  }
  LD_Reset = false;
  findMode();
}
void resetLaunchTime() {
  LT_Reset = true;
  displayMatrix(LT_Matrix);
  for (int i = 0; i < 4; i++) {
    dispVals[0][i] = 10;
    dispVals[1][i] = 10;
    dispDP[0][i] = false;
    dispDP[1][i] = false;
  }
  dispDP[0][3] = true;
  dispDP[1][1] = true;
  unsigned long lastBlink = millis();
  const unsigned long blinkInterval = 500;
  bool matrixOn = true;
  
  String inpString = "";
  bool inputDone = false;

  while (!inputDone) {
    char key = keypad.getKey();
    if (key) {
      switch (key) {
        case '0' ... '9':
          if (inpString.length() < 6) {
            inpString += key;
            showInputPreview(inpString);
          }
          break;
          
        case '#':
          if (inpString.length() == 6) {
            parseAndSetLaunchTime(inpString);
            inputDone = true;
          } else {
            LT_Reset = false;
            return;}
          break;

        case 'D':
          if (inpString.length() > 0) {
            inpString.remove(inpString.length() - 1);
            showInputPreview(inpString);
          }
          break;
      }
    }

    if (millis() - lastBlink >= blinkInterval) {
      matrixOn = !matrixOn;
      lastBlink = millis();

      if (matrixOn) {
        displayMatrix(LT_Matrix);
      } else {
        displayMatrix(matrixOFF);
      }
    }

    refreshDisplays();
    delay(1);
  }
  LT_Reset = false;
  findMode();
}
void showInputPreview(const String& str) {
  String displayStr = str;
  if (LD_Reset) {
    while (displayStr.length() < 8) displayStr = displayStr + " ";
  } else if (LT_Reset) {
    while (displayStr.length() < 6) displayStr = displayStr + " ";
  } else {
    while (displayStr.length() < 8) displayStr = " " + displayStr;
  }

  for (int i = 0; i < 4; i++) {
    char c = displayStr[displayStr.length() - 8 + i];
    dispVals[0][i] = (c >= '0' && c <= '9') ? c - '0' : 10;
  }
  for (int i = 0; i < 4; i++) {
    char c = displayStr[displayStr.length() - 4 + i];
    dispVals[1][i] = (c >= '0' && c <= '9') ? c - '0' : 10;
  }
}
void parseAndSetLaunchTime(const String& s) {
  if (s.length() < 6) return;
  
  long newLZero[6] = {-1, -1, -1, -1, -1, -1};
  
  if (s.length() == 6) {
    newLZero[3] = s.substring(0,2).toInt();
    newLZero[4] = s.substring(2,4).toInt();
    newLZero[5] = s.substring(4,6).toInt();
  }
  else if (s.length() == 8) {
    newLZero[0] = s.substring(0,4).toInt();
    newLZero[1] = s.substring(4,6).toInt();
    newLZero[2] = s.substring(6,8).toInt();
  }

  for (int i = 0; i < 5; i++) {
    if (newLZero[i] != -1)  L_Zero[i] = newLZero[i];
  }

  launchTime = toUnix(L_Zero);
}

void pauseCountdown() {
  DateTime pauseTime = rtc.now();
  if (pauseTime.unixtime() >= launchTime) {
    return;
  }
  pauseActive = true;
  displayMatrix(L_Pause);
  do {
    keypad.getKey();
    refreshDisplays();
    delay(1);
  } while (!exitPause);
  DateTime now = rtc.now();
  long pauseDuration = now.unixtime() - pauseTime.unixtime();
  launchTime = launchTime + pauseDuration;
}

void showTime() {
  timeModeActive = true;
  displayMatrix(timeMatrix);
  unsigned long modeStart = millis();
  dispDP[0][0] = false;
  dispDP[0][1] = false;
  dispDP[0][2] = false;
  dispDP[0][3] = true;
  dispDP[1][0] = false;
  dispDP[1][1] = true;
  dispDP[1][2] = false;
  dispDP[1][3] = false;
  dispVals[0][0] = 10;
  dispVals[0][1] = 10;
  do {
    DateTime time = rtc.now();
    keypad.getKey();
    uint8_t hour = time.hour();
    uint8_t min = time.minute();
    uint8_t sec = time.second();
    dispVals[0][2] = hour / 10;
    dispVals[0][3] = hour % 10;
    dispVals[1][0] = min / 10;
    dispVals[1][1] = min % 10;
    dispVals[1][2] = sec / 10;
    dispVals[1][3] = sec % 10;
    refreshDisplays();
    delay(1);
  } while (!exitTimeMode && (millis() - modeStart) < (autoCancel * 1000));
  timeModeActive = false;
  exitTimeMode = false;
}

void findMode() {
  if (displayOn == false) {
    displayMatrix(matrixOFF);
    for (int i = 0; i < 4; i++) {
      dispVals[0][i] = 10;
      dispVals[1][i] = 10;
      dispDP[0][i] = false;
      dispDP[1][i] = false;
    }
  } else {
    if (hourMode == false) updateDayMode();
    if (hourMode == true) updateHourMode();
  }
}

void updateDayMode() {
  // Set DPs
  dispDP[0][0] = false;
  dispDP[0][1] = true;
  dispDP[0][2] = false;
  dispDP[0][3] = true;
  dispDP[1][0] = false;
  dispDP[1][1] = true;
  dispDP[1][2] = false;
  dispDP[1][3] = false;

  DateTime now = rtc.now();
  uint32_t nowUnix = now.unixtime();
  uint32_t diff;
  const uint8_t* newMatrix;

  if (nowUnix < launchTime) {
    diff = launchTime - nowUnix;
    newMatrix = L_Minus;
  } else {
    diff = nowUnix - launchTime;
    newMatrix = L_Plus;
  }
  displayMatrix(newMatrix);

  long days  = diff / 86400;
  long hours = (diff % 86400) / 3600;
  int mins   = (diff % 3600) / 60;
  int secs   = diff % 60;

  if (days > 99) {
    days  = 99;
    hours = 23;
    mins  = 59;
    secs  = 59;
  }

  dispVals[0][0] = days / 10;
  if (days < 10) dispVals[0][0] = 10;
  dispVals[0][1] = days % 10;
  if (days == 0) {
    dispVals[0][1] = 10;
    dispDP[0][1] = false;
  }
  dispVals[0][2] = hours / 10;
  dispVals[0][3] = hours % 10;
  dispVals[1][0] = mins / 10;
  dispVals[1][1] = mins % 10;
  dispVals[1][2] = secs / 10;
  dispVals[1][3] = secs % 10;
}

void updateHourMode() {
  // Set DPs
  dispDP[0][0] = false;
  dispDP[0][1] = false;
  dispDP[0][2] = false;
  dispDP[0][3] = true;
  dispDP[1][0] = false;
  dispDP[1][1] = true;
  dispDP[1][2] = false;
  dispDP[1][3] = false;

  DateTime now = rtc.now();
  uint32_t nowUnix = now.unixtime();
  uint32_t diff;
  const uint8_t* newMatrix;

  if (nowUnix < launchTime) {
    diff = launchTime - nowUnix;
    newMatrix = L_Minus;
  } else {
    diff = nowUnix - launchTime;
    newMatrix = L_Plus;
  }
  displayMatrix(newMatrix);

  long hours = diff / 3600;
  int mins   = (diff % 3600) / 60;
  int secs   = diff % 60;

  if (hours > 999) {
    hours = 999;
    mins  = 59;
    secs  = 59;
  }

  dispVals[0][0] = 10;
  dispVals[0][1] = hours / 100;
  if (hours < 100) dispVals[0][1] = 10;
  dispVals[0][2] = hours / 10;
  dispVals[0][3] = hours % 10;
  dispVals[1][0] = mins / 10;
  dispVals[1][1] = mins % 10;
  dispVals[1][2] = secs / 10;
  dispVals[1][3] = secs % 10;
}

void displayMatrix(const uint8_t* image) {
  if (image == lastMatrix) return;
  for (uint8_t i = 0; i < 8; i++) {
    matrixDisplay.setRow(0, i, image[i]);
  }
  lastMatrix = image;
}

void refreshDisplays() {
  for (uint8_t i = 0; i < 2; i++) {
    for (uint8_t d = 0; d < 4; d++) {
      uint8_t num = dispVals[i][d];
      uint8_t dp  = dispDP[i][d] ? 1 : 0;

      if (i == 0) {
        PORTC = segmentBytes[i][num][dp];
      } else {
        PORTA = segmentBytes[i][num][dp];
      }

      digitalWrite(digitPins[i][d], LOW);   // enable digit
      delayMicroseconds(800);
      digitalWrite(digitPins[i][d], HIGH);  // disable digit
    }
  }
}

void setup() {
  launchTime = toUnix(L_Zero);
  // keypad shit
  keypad.addEventListener(keypadEvent);
  keypad.setHoldTime(1500);
  // matrix shit
  matrixDisplay.clearDisplay(0);
  matrixDisplay.shutdown(0, false);
  matrixDisplay.setIntensity(0, 0.9);
  // Configure matrix pins
  pinMode(matrixPins[0], OUTPUT);
  pinMode(matrixPins[1], OUTPUT);
  pinMode(matrixPins[2], OUTPUT);
  digitalWrite(matrixPins[0], LOW);
  digitalWrite(matrixPins[1], LOW);
  digitalWrite(matrixPins[2], LOW);
  // Configure all display pins as outputs
  for (int i = 0; i < 4; i++) {
    pinMode(digitPins[0][i], OUTPUT);
    pinMode(digitPins[1][i], OUTPUT);
    digitalWrite(digitPins[0][i], HIGH);
    digitalWrite(digitPins[1][i], HIGH);
  }
  for (int i = 0 ; i < 8; i++) {
    pinMode(segmentPins[0][i], OUTPUT);
    pinMode(segmentPins[1][i], OUTPUT);
    digitalWrite(segmentPins[0][i], LOW);
    digitalWrite(segmentPins[1][i], LOW);
  }
  initSegmentBytes();

  Wire.begin();
  if (!rtc.begin()) {
    while (1) delay(10);
  }
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  updateDayMode();
  lastUpdate = millis();
}

void loop() {
  char key = keypad.getKey();
  if (millis() - lastUpdate >= 1000) {
    findMode();
    lastUpdate = millis();
  }
  refreshDisplays();
}