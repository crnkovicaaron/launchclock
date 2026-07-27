#include <Wire.h>
#include <RTClib.h>
#include <Keypad.h>
#include <LedControl.h>
#include <set.h>
#include <EEPROM.h>

RTC_DS3231 rtc;

// L-0 time. Format: {Y, M, D, h, m, s}
uint32_t L_Zero[6] = {2026, 4, 1, 18, 35, 12};
uint32_t launchTime;

uint32_t CT_Set[6] = {2000, 1, 1, 0, 0, 0}; // for CT set purposes

const uint8_t autoCancel = 10; // time mode autocancel (in seconds)
const uint32_t blinkInterval = 500; // matrix blink rate in ms (when applicable)
uint8_t brightnessMode = 1; // initial brightness mode
bool hourMode = false; // Start in day mode
bool displayOn = true; // Start with display on
bool pauseActive = false;
bool exitPause = false;
bool showTimeActive = false;
bool exitShowTime = false;
bool bypassShowTime = false;
bool showLDActive = false;
bool exitShowLD = false;
bool showLTActive = false;
bool exitShowLT = false;
bool CT_Reset = false;
bool CD_Reset = false;
bool LD_Reset = false;
bool LT_Reset = false;
bool resetMatrix = false;
bool errorModeActive = false;
bool countDelayed = false;
bool bypass = false;
bool timeValid;
const uint8_t* lastMatrix = nullptr;

const uint8_t DIN_PIN = A0;
const uint8_t CLK_PIN = A1;
const uint8_t CS_PIN = A2;
const uint8_t rowPins[4] = {2, 3, 4, 5};
const uint8_t colPins[4] = {6, 7, 8, 9};

const char keys[4][4] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
const uint8_t displayIntensity[3][2] = {{2,0.9},{8,2},{15,5}}; // for brightnessMode 0,1,2
const uint8_t L_Plus[8] = {
  0b10000000,
  0b10000000,
  0b10000000,
  0b10000010,
  0b10000111,
  0b10000010,
  0b10000000,
  0b11110000
};
const uint8_t L_Minus[8] = {
  0b10000000,
  0b10000000,
  0b10000000,
  0b10000000,
  0b10000111,
  0b10000000,
  0b10000000,
  0b11110000
};
const uint8_t L_Pause[8] = {
  0b10000000,
  0b10000000,
  0b10000000,
  0b10000101,
  0b10000101,
  0b10000101,
  0b10000000,
  0b11110000
};
const uint8_t CT_Matrix[8] = {
  0b11100111,
  0b10000010,
  0b10000010,
  0b10000010,
  0b10000010,
  0b10000010,
  0b10000010,
  0b11100010
};
const uint8_t CD_Matrix[8] = {
  0b11101110,
  0b10001001,
  0b10001001,
  0b10001001,
  0b10001001,
  0b10001001,
  0b10001001,
  0b11101110
};
const uint8_t LT_Matrix[8] = {
  0b10011111,
  0b10000100,
  0b10000100,
  0b10000100,
  0b10000100,
  0b10000100,
  0b10000100,
  0b11110100
};
const uint8_t LD_Matrix[8] = {
  0b10001110,
  0b10001001,
  0b10001001,
  0b10001001,
  0b10001001,
  0b10001001,
  0b10001001,
  0b11101110
};
const uint8_t ER_Matrix[8] = {
  0b11101111,
  0b10001001,
  0b10001001,
  0b10001110,
  0b11101010,
  0b10001001,
  0b10001001,
  0b11101001
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
uint8_t dispVals[2][4] = {
  {0, 0, 0, 0},
  {0, 0, 0, 0}
};
bool dispDP[2][4]  = {
  {false, true, false, true},
  {false, true, false, false}
};
uint8_t prevDispVals[2][4] = {{11,11,11,11},{11,11,11,11}};  // 11 to force initial update
bool prevDispDP[2][4]   = {{true,true,true,true},{true,true,true,true}};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);
LedControl lc = LedControl(DIN_PIN, CLK_PIN, CS_PIN, 2);
uint32_t lastUpdate = 0;


void errorMode(String str) {
  errorModeActive = true;
  displayMatrix(ER_Matrix);
  resetMatrix = false;
  for (int i = 0; i < 4; i++) {
    dispVals[0][i] = 10;
    dispVals[1][i] = 10;
    dispDP[0][i] = false;
    dispDP[1][i] = false;
  }
  refreshDisplays();
  if (str == "ND") {
    uint32_t lastBlink = millis();
    bool matrixOn = true;
    while (true) {
      if (millis() - lastBlink >= blinkInterval) {
        matrixOn = !matrixOn;
        lastBlink = millis();
        if (matrixOn) {
          displayMatrix(ER_Matrix);
        } else {
          displayMatrix(matrixOFF);
        }
      }
    }
  }
  do {
    char key = keypad.getKey();
    if (resetMatrix) {
      for (int i = 0; i < 4; i++) {
        dispVals[0][i] = 10;
        dispVals[1][i] = 10;
        dispDP[0][i] = false;
        dispDP[1][i] = false;
      }
      refreshDisplays();
      displayMatrix(ER_Matrix);
      resetMatrix = false;
    }
    if (key == 'C' && str == "CT") {
      resetCurrentTime();
      resetMatrix = true;
    }
    if (key == 'C' && str == "CD") {
      resetCurrentDate();
      resetMatrix = true;
    }
    if (key == '*' && (str == "LD" || str == "LX")) {
      resetLaunchDate();
      resetMatrix = true;
    }
    if (key == '#' && (str == "LT" || str == "LX")) {
      resetLaunchTime();
      resetMatrix = true;
    }
    delay(1);
  } while (!timeValid);
  errorModeActive = false;
}
uint32_t toUnix(uint32_t T[]) {
  DateTime launch(T[0], T[1], T[2], T[3], T[4], T[5]);
  uint32_t t = launch.unixtime();
  return(t);
}
bool checkTimeValid(uint32_t T[]) {
  if (T[0] < 2000 || T[0] > 2100) return false;
  if (T[1] < 1 || T[1] > 12) return false;
  Set maxMonths;
  maxMonths.add(1); maxMonths.add(3); maxMonths.add(5); maxMonths.add(7); maxMonths.add(8); maxMonths.add(10); maxMonths.add(12);
  if (maxMonths.has(T[1])) {
    if (T[2] > 31) return false;
  } else if (T[1] != 2) {
    if (T[2] > 30) return false;
  } else {
    if (T[0] % 400 == 0 || (T[0] % 4 == 0 && T[0] % 100 != 0)) {
      if (T[2] > 29) return false;
    } else {
      if (T[2] > 28) return false;
    }
  }
  if (T[2] < 1) return false;
  if (T[3] > 23) return false;
  if (T[4] > 59) return false;
  if (T[5] > 59) return false;
  return true;
}

void keypadEvent(KeypadEvent key) {
  if (LT_Reset || LD_Reset || CT_Reset || CD_Reset || !timeValid) return;
  switch (keypad.getState()) {
    case PRESSED:
      if (key == 'C' && pauseActive) {
        pauseActive = false;
        exitPause = true;
      }
      if (key == 'D' && pauseActive) exitPause = true;
      break;
    
    case HOLD:
      if (key == 'A' && !pauseActive && !showTimeActive && !showLDActive && !showLTActive && displayOn) {
        displayOn = false;
        findMode();
      }
      if (key == 'C' && !pauseActive && displayOn && !showTimeActive && !showLDActive && !showLTActive) {
        resetCurrentTime();
        bypassShowTime = true;
      }
      if (key == 'D' && !pauseActive) {
        launchTime = toUnix(L_Zero);
        lc.setLed(1, 7, 7, false);
        countDelayed = false;
        exitPause = true;
        findMode();
      }
      if (key == '#' && !pauseActive && displayOn && !showTimeActive && !showLDActive && !showLTActive) {
        resetLaunchTime();
        exitShowLT = true;
      }
      if (key == '*' && !pauseActive && displayOn && !showTimeActive && !showLDActive && !showLTActive) {
        resetLaunchDate();
        exitShowLD = true;
      }
      break;
    
    case RELEASED:
      if (key == 'A' && !pauseActive && !showTimeActive && !showLDActive && !showLTActive) {
        if (!displayOn && !bypass) bypass = true;
        else if (!displayOn && bypass) {
          displayOn = true;
          bypass = false;
          findMode();
        } else {
          if (exitShowLD) {
          exitShowLD = false;
          } else {
              brightnessMode++;
              if (brightnessMode == 3) brightnessMode = 0;
              for (int i = 0; i < 2; i++) {
                lc.setIntensity(i, displayIntensity[brightnessMode][i]);
              }
            findMode();
          }
        }
      }
      if (key == 'B' && !pauseActive && !showTimeActive && !showLDActive && !showLTActive && displayOn) {
      hourMode = !hourMode;
      findMode();
      }
      if (key == 'C' && !pauseActive && !showLDActive && !showLTActive && displayOn) {
        if (bypass) bypass = false;
        else {
          if (exitPause) exitPause = false;
          else if (bypassShowTime) bypassShowTime = false;
          else if (!showTimeActive) {
            showTime();
          } else {
            exitShowTime = true;
          }
        }
      }
      if (key == 'D') {
        if (!pauseActive && !exitPause && !showTimeActive && !showLDActive && !showLTActive && displayOn) pauseCountdown();
        else {
          exitPause = false;
          pauseActive = false;
        } 
      }
      if (key == '*' && !pauseActive && !showTimeActive && displayOn) {
        if (showLTActive) {
        exitShowLT = true;
        }
        if (!showLDActive && !exitShowLD) {
          showLaunchDate();
        } else if (showLDActive && !exitShowLD) exitShowLD = true;
        else {
          showLDActive = false;
          exitShowLD = false;
        }
      }
      if (key == '#' && !pauseActive && !showTimeActive && displayOn) {
        if (showLDActive) {
        exitShowLD = true;
        }
        if (!showLTActive && !exitShowLT) {
          showLaunchTime();
        } else if (showLTActive && !exitShowLT) exitShowLT = true;
        else {
          showLTActive = false;
          exitShowLT = false;
        }
      }
      break;
    default:
      break;
  }
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

void resetCurrentTime() {
  CT_Reset = true;
  displayMatrix(CT_Matrix);
  for (int i = 0; i < 4; i++) { // turn displays off
    dispVals[0][i] = 10;
    dispVals[1][i] = 10;
    dispDP[0][i] = false;
    dispDP[1][i] = false;
  }
  dispDP[0][3] = true;
  dispDP[1][1] = true;
  uint32_t lastBlink = millis();
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

        case 'B':
          inpString = "";
          showInputPreview(inpString);
          break;
          
        case 'C':
          if (inpString.length() == 6) {
            parseAndSet_CT(inpString);
            inputDone = true;
          } else {
            CT_Reset = false;
            resetMatrix = true;
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
        displayMatrix(CT_Matrix);
      } else {
        displayMatrix(matrixOFF);
      }
    }
    refreshDisplays();
    delay(10);
  }
  CT_Reset = false;
  timeValid = checkTimeValid(CT_Set);
  if (errorModeActive) return;
  if (!timeValid) errorMode("CT");
  countDelayed = false;
  DateTime newTime(CT_Set[0], CT_Set[1], CT_Set[2], CT_Set[3], CT_Set[4], CT_Set[5]); // create datetime with new time
  rtc.adjust(newTime);
  findMode();
}
void resetCurrentDate() {
  // This function is only called if loss of RTC power is detected. Not accessible in standard UI
  CD_Reset = true;
  displayMatrix(CD_Matrix);
  for (int i = 0; i < 4; i++) {
    dispVals[0][i] = 10;
    dispVals[1][i] = 10;
    dispDP[0][i] = false;
    dispDP[1][i] = false;
  }
  dispDP[0][3] = true;
  dispDP[1][1] = true;
  uint32_t lastBlink = millis();
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
          
        case 'C':
          if (inpString.length() == 8) {
            parseAndSet_CD(inpString);
            inputDone = true;
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
        displayMatrix(CD_Matrix);
      } else {
        displayMatrix(matrixOFF);
      }
    }
    refreshDisplays();
    delay(10);
  }
  CD_Reset = false;
  timeValid = checkTimeValid(CT_Set);
  if (errorModeActive) return;
  if (!timeValid) errorMode("CD");
}
void resetLaunchDate() {
  LD_Reset = true;
  bool addDay = false;
  uint32_t newL_Zero[6] = {L_Zero[0], L_Zero[1], L_Zero[2], L_Zero[3], L_Zero[4], L_Zero[5]};
  uint8_t daysAdded = 0;
  bool validity;
  displayMatrix(LD_Matrix);
  for (int i = 0; i < 4; i++) {
    dispVals[0][i] = 10;
    dispVals[1][i] = 10;
    dispDP[0][i] = false;
    dispDP[1][i] = false;
  }
  dispDP[0][3] = true;
  dispDP[1][1] = true;
  uint32_t lastBlink = millis();
  bool matrixOn = true;

  String inpString = "";
  bool inputDone = false;

  while (!inputDone) {
    char key = keypad.getKey();
    if (key) {
      switch (key) {
        case '0' ... '9':
          if (addDay) break;
          if (inpString.length() < 8) {
            inpString += key;
            showInputPreview(inpString);
          }
          break;
        
        case 'A':
          if (!addDay) {
            for (int i = 0; i < 4; i++) {
              dispVals[0][i] = 10;
              dispVals[1][i] = 10;
              dispDP[0][i] = false;
              dispDP[1][i] = false;
            }
            addDay = true;
            inpString = "";
          }
          ++daysAdded;
          if (daysAdded > 7) {
            daysAdded = 7;
            break;
          }
          dispVals[1][3] = daysAdded;
          refreshDisplays();
          ++newL_Zero[2];
          validity = checkTimeValid(newL_Zero);
          if (!validity) {
            newL_Zero[2] = 1;
            ++newL_Zero[1];
          }
          validity = checkTimeValid(newL_Zero);
          if (!validity) {
            newL_Zero[1] = 1;
            ++newL_Zero[0];
          }
          break;
        
        case 'B':
          if (addDay) {
            addDay = false;
            daysAdded = 0;
            dispVals[1][3] = 10;
            dispDP[0][3] = true;
            dispDP[1][1] = true;
            refreshDisplays();
          } else {
            inpString = "";
            showInputPreview(inpString);
          }
          break;
        
        case 'C':
          if (addDay) {
            LD_Reset = false;
            resetMatrix = true;
            bypass = true;
            return;
          } else break;

        case '*':
          if (addDay) {
            validity = checkTimeValid(newL_Zero);
            if (!validity) errorMode("LD");
            for (int i = 0; i < 6; i++) {
              L_Zero[i] = newL_Zero[i];
            }
            EEPROM.put(0, L_Zero);
            launchTime = toUnix(L_Zero);
            LD_Reset = false;
            resetMatrix = true;
            return;
          }
          if (inpString.length() == 8) {
            parseAndSetL_Zero(inpString);
            inputDone = true;
          } else {
            LD_Reset = false;
            resetMatrix = true;
            return;
          }
          break;
        
        case 'D':
          if (addDay) {
            for (int i = 0; i < 6; i++) {
              newL_Zero[i] = L_Zero[i];
            }
            daysAdded = 0;
            dispVals[1][3] = 0;
            refreshDisplays();
            break;
          }
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
    delay(10);
  }
  LD_Reset = false;
  timeValid = checkTimeValid(L_Zero);
  if (errorModeActive) return;
  if (!timeValid) errorMode("LD");
  launchTime = toUnix(L_Zero);
  countDelayed = false;
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
  uint32_t lastBlink = millis();
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
        
        case 'B':
          inpString = "";
          showInputPreview(inpString);
          break;
          
        case '#':
          if (inpString.length() == 6) {
            parseAndSetL_Zero(inpString);
            inputDone = true;
          } else {
            LT_Reset = false;
            resetMatrix = true;
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
        displayMatrix(LT_Matrix);
      } else {
        displayMatrix(matrixOFF);
      }
    }
    refreshDisplays();
    delay(10);
  }
  LT_Reset = false;
  timeValid = checkTimeValid(L_Zero);
  if (errorModeActive) return;
  if (!timeValid) errorMode("LT");
  launchTime = toUnix(L_Zero);
  countDelayed = false;
  findMode();
}
void showInputPreview(const String& str) {
  String displayStr = str;
  if (LD_Reset || CD_Reset) {
    while (displayStr.length() < 8) displayStr = displayStr + " ";
  } else if (LT_Reset || CT_Reset) {
    while (displayStr.length() < 6) displayStr = displayStr + " ";
  } else errorMode("ND");

  for (int i = 0; i < 4; i++) {
    char c = displayStr[displayStr.length() - 8 + i];
    dispVals[0][i] = (c >= '0' && c <= '9') ? c - '0' : 10;
  }
  for (int i = 0; i < 4; i++) {
    char c = displayStr[displayStr.length() - 4 + i];
    dispVals[1][i] = (c >= '0' && c <= '9') ? c - '0' : 10;
  }
}
void parseAndSet_CT(const String& s) {
  if (s.length() < 6) return;

  uint32_t newCT[3] = {0, 0, 0};

  newCT[0] = s.substring(0,2).toInt();
  newCT[1] = s.substring(2,4).toInt();
  newCT[2] = s.substring(4,6).toInt();

  DateTime now = rtc.now(); // current time on rtc

  // Writing new time to CT_Set (global) for validity verification
  if (CT_Set[0] == 2000 && CT_Set[1] == 1 && CT_Set[2] == 1) {
    CT_Set[0] = now.year();
    CT_Set[1] = now.month();
    CT_Set[2] = now.day();
  }
  for (int i = 3; i < 6; i++) {
    CT_Set[i] = newCT[i-3];
  }
}
void parseAndSet_CD(const String& s) {
  if (s.length() < 8) return;

  uint32_t newCD[3] = {0, 0, 0};

  newCD[0] = s.substring(0,4).toInt();
  newCD[1] = s.substring(4,6).toInt();
  newCD[2] = s.substring(6,8).toInt();
  for (int i = 0; i < 3; i++) {
    CT_Set[i] = newCD[i];
  }
}
void parseAndSetL_Zero(const String& s) {
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
  bool changed = false;
  for (int i = 0; i < 6; i++) {
    if (newLZero[i] != -1 && newLZero[i] != L_Zero[i]) {
      L_Zero[i] = newLZero[i];
      changed = true;
    }
  }

  if (changed) {
    EEPROM.put(0, L_Zero);
  }
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
  if (!pauseActive) return;
  DateTime now = rtc.now();
  long pauseDuration = now.unixtime() - pauseTime.unixtime();
  launchTime = launchTime + pauseDuration;
  countDelayed = true;
}

void showLaunchDate() {
  showLDActive = true;
  displayMatrix(LD_Matrix);
  uint32_t modeStart = millis();
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
  uint32_t year = L_Zero[0];
  uint8_t month = L_Zero[1];
  uint8_t day = L_Zero[2];
  dispVals[0][0] = year / 1000;
  dispVals[0][1] = (year % 1000) / 100;
  dispVals[0][2] = (year % 100) / 10;
  dispVals[0][3] = year % 10;
  dispVals[1][0] = month / 10;
  dispVals[1][1] = month % 10;
  dispVals[1][2] = day / 10;
  dispVals[1][3] = day % 10;
  do {
    keypad.getKey();
    refreshDisplays();
    delay(1);
  } while (!exitShowLD && (millis() - modeStart) < (autoCancel * 1000));
  showLDActive = false;
  exitShowLD = false;
}
void showLaunchTime() {
  showLTActive = true;
  displayMatrix(LT_Matrix);
  uint32_t modeStart = millis();
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
  uint32_t hour = L_Zero[3];
  uint8_t minute = L_Zero[4];
  uint8_t second = L_Zero[5];
  dispVals[0][2] = hour / 10;
  dispVals[0][3] = hour % 10;
  dispVals[1][0] = minute / 10;
  dispVals[1][1] = minute % 10;
  dispVals[1][2] = second / 10;
  dispVals[1][3] = second % 10;
  do {
    keypad.getKey();
    refreshDisplays();
    delay(1);
  } while (!exitShowLT && (millis() - modeStart) < (autoCancel * 1000));
  showLTActive = false;
  exitShowLT = false;
}
void showTime() {
  showTimeActive = true;
  displayMatrix(CT_Matrix);
  uint32_t modeStart = millis();
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
  } while (!exitShowTime && (millis() - modeStart) < (autoCancel * 1000));
  showTimeActive = false;
  exitShowTime = false;
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
  dispVals[0][2] = (hours / 10) % 10;
  dispVals[0][3] = hours % 10;
  dispVals[1][0] = mins / 10;
  dispVals[1][1] = mins % 10;
  dispVals[1][2] = secs / 10;
  dispVals[1][3] = secs % 10;

}

void displayMatrix(const uint8_t* image) {
  if (image == lastMatrix) return;
  for (uint8_t i = 0; i < 8; i++) {
    lc.setRow(1, i, image[i]);
  }
  lastMatrix = image;
  if (image == L_Plus || image == L_Minus || image == L_Pause) {
    lc.setLed(1, 7, 7, countDelayed);
  }
}
void refreshDisplays() {
  bool changed = false;
  
  for (int d = 0; d < 2; d++) {
    for (int i = 0; i < 4; i++) {
      if (dispVals[d][i] != prevDispVals[d][i] || 
          dispDP[d][i] != prevDispDP[d][i]) {
        
        if (dispVals[d][i] == 10) {
          lc.setRow(0, i + 4*d, dispDP[d][i] ? 0x80 : 0x00);   // blank the digit
        } else {
          lc.setDigit(0, i + 4*d, dispVals[d][i], dispDP[d][i]);
        }        
        prevDispVals[d][i] = dispVals[d][i];
        prevDispDP[d][i]   = dispDP[d][i];
        changed = true;
      }
    }
  }
  
}

void setup() {
  // keypad shit
  keypad.addEventListener(keypadEvent);
  keypad.setHoldTime(1500);
  // display shit
  pinMode(10, OUTPUT);
  pinMode(11, OUTPUT);
  pinMode(13, OUTPUT);
  digitalWrite(10, LOW);
  digitalWrite(11, LOW);
  digitalWrite(13, LOW);
  lc.clearDisplay(0);
  lc.shutdown(0, false);
  lc.setIntensity(0, 8);
  lc.clearDisplay(1);
  lc.shutdown(1, false);
  lc.setIntensity(1, 2);

  memset(prevDispVals, 11, sizeof(prevDispVals));  // ensure first refreshDisplays() writes everything

  Wire.begin();
  if (!rtc.begin()) {
    while (1) delay(10);
  }

  uint32_t savedLZero[6];
  EEPROM.get(0, savedLZero);
  if (savedLZero[0] >= 2000 && savedLZero[0] <= 2100) {
    for (int i = 0; i < 6; i++) {
      L_Zero[i] = savedLZero[i];
    }
  }
  timeValid = checkTimeValid(L_Zero);
  if (!timeValid) errorMode("LX");
  launchTime = toUnix(L_Zero);

  if (rtc.lostPower()) {
    resetCurrentDate();
    resetCurrentTime();
    bypassShowTime = true;
  }

  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  
  updateDayMode();
  lastUpdate = millis();
}

void loop() {
  keypad.getKey();
  if (millis() - lastUpdate >= 1000) {
    findMode();
    lastUpdate = millis();
  }
  refreshDisplays();
}