#include <Wire.h>
#include <RTClib.h>
#include <Keypad.h>

RTC_DS1307 rtc;

// L-0 time. Format: {Y, M, D, m, S}
long L_Zero[6] = {2026, 4, 19, 6, 45, 0};
uint32_t launchTime;

const uint8_t timeModePin = 10; // time mode LED indicator pin
const uint8_t pausedPin = 11; // countdown paused LED indicator pin
const unsigned long autoCancel = 10; // time mode autocancel time (in seconds)
bool hourMode = false; // Start in day mode
bool displayOn = true; // Start with display on
bool pauseActive = false; 
bool timeModeActive = false;
bool exitPause = false;
bool exitTimeMode = false;

// Keypad pins
const byte rowPins[4] = {9, 8, 7, 6};
const byte colPins[4] = {5, 4, 3, 2};

// Display 1 pins (DD.HH.)
const uint8_t digitPins1[] = {42, 43, 44, 45};
const uint8_t segmentPins1[] = {30, 31, 32, 33, 34, 35, 36, 37}; // order: a, b, c, d, e, f, g, DP

// Display 2 pins (MM.SS)
const uint8_t digitPins2[] = {38, 39, 40, 41};
const uint8_t segmentPins2[] = {22, 23, 24, 25, 26, 27, 28, 29}; // order: a, b, c, d, e, f, g, DP

// Defining keypad buttons
const char keys[4][4] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};

// Segmentation (DP = 0 initially) + 10 = blank
const byte segmentPatterns[11][8] = {
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

// Create keypad object
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);

// Current values to display (updated from RTC every second) (default is day mode)
uint8_t disp1Vals[4] = {0, 0, 0, 0};     // DDHH
bool disp1DP[4]  = {false, true, false, true};  // DD.HH.

uint8_t disp2Vals[4] = {0, 0, 0, 0};     // MMSS
bool disp2DP[4]  = {false, true, false, false}; // MM.SS

unsigned long lastUpdate = 0;

uint32_t toUnix(long T[]) {
  DateTime launch(T[0], T[1], T[2], T[3], T[4], T[5]);
  uint32_t t = launch.unixtime();
  return(t);
}

void keypadEvent(KeypadEvent key) {
  switch (keypad.getState()) {
    case PRESSED:
      if (key == 'D') {
        if(!pauseActive) {
          pauseCountdown();
        } else {
          exitPause = true;
        }
      }
      break;
    case HOLD:
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
      break;
    default:
      break;
  }
}

void pauseCountdown() {
  DateTime pauseTime = rtc.now();
  if (pauseTime.unixtime() >= launchTime) {
    return;
  }
  pauseActive = true;
  digitalWrite(pausedPin, HIGH);

  do {
    keypad.getKey();
    refreshDisplay1();
    refreshDisplay2();
    delay(1);
  } while (!exitPause);
  exitPause = false;

  DateTime now = rtc.now();
  long pauseDuration = now.unixtime() - pauseTime.unixtime();
  launchTime = launchTime + pauseDuration;
  digitalWrite(pausedPin, LOW);
  pauseActive = false;
}

void showTime() {
  timeModeActive = true;
  unsigned long modeStart = millis();
  digitalWrite(timeModePin, HIGH); // indicator ON
  disp1DP[0] = false;
  disp1DP[1] = false;
  disp1DP[2] = false;
  disp1DP[3] = true;
  disp2DP[0] = false;
  disp2DP[1] = true;
  disp2DP[2] = false;
  disp2DP[3] = false;
  disp1Vals[0] = 10;
  disp1Vals[1] = 10;
  
  do {
    DateTime time = rtc.now();
    keypad.getKey();
    uint8_t hour = time.hour();
    uint8_t min = time.minute();
    uint8_t sec = time.second();
    disp1Vals[2] = hour / 10;
    disp1Vals[3] = hour % 10;
    disp2Vals[0] = min / 10;
    disp2Vals[1] = min % 10;
    disp2Vals[2] = sec / 10;
    disp2Vals[3] = sec % 10;
    refreshDisplay1();
    refreshDisplay2();
    delay(1);
  } while (!exitTimeMode && (millis() - modeStart) < (autoCancel * 1000));

  digitalWrite(timeModePin, LOW); // indicator OFF
  timeModeActive = false;
  exitTimeMode = false;
}

void findMode() {
  if (displayOn == false) {
      for (int i = 0; i < 4; i++) {
        disp1Vals[i] = 10;
        disp2Vals[i] = 10;
        disp1DP[i] = false;
        disp2DP[i] = false;
      }
    } else {
      if (hourMode == false) updateDayMode();
      if (hourMode == true) updateHourMode();
    }
}

void updateDayMode() {
  // Set DPs
  disp1DP[0] = false;
  disp1DP[1] = true;
  disp1DP[2] = false;
  disp1DP[3] = true;
  disp2DP[0] = false;
  disp2DP[1] = true;
  disp2DP[2] = false;
  disp2DP[3] = false;

  DateTime now = rtc.now();
  uint32_t nowUnix = now.unixtime();

  uint32_t diff;
  if (nowUnix < launchTime) {
    diff = launchTime - nowUnix;   // still counting down (L-)
  } else {
    diff = nowUnix - launchTime;   // counting up (T+) or at zero
  }

  long days  = diff / 86400;
  long hours = (diff % 86400) / 3600;
  int mins   = (diff % 3600) / 60;
  int secs   = diff % 60;

  // Max out at 99.23.59.59 as requested (no further rollover)
  if (days > 99) {
    days  = 99;
    hours = 23;
    mins  = 59;
    secs  = 59;
  }

  // Display 1: DD.HH.
  disp1Vals[0] = days / 10;
  if (days < 10) disp1Vals[0] = 10;
  disp1Vals[1] = days % 10;
  if (days == 0) {
    disp1Vals[1] = 10;
    disp1DP[1] = false;
  }
  disp1Vals[2] = hours / 10;
  disp1Vals[3] = hours % 10;

  // Display 2: MM.SS
  disp2Vals[0] = mins / 10;
  disp2Vals[1] = mins % 10;
  disp2Vals[2] = secs / 10;
  disp2Vals[3] = secs % 10;
}

void updateHourMode() {
  // Set DPs
  disp1DP[0] = false;
  disp1DP[1] = false;
  disp1DP[2] = false;
  disp1DP[3] = true;
  disp2DP[0] = false;
  disp2DP[1] = true;
  disp2DP[2] = false;
  disp2DP[3] = false;

  DateTime now = rtc.now();
  uint32_t nowUnix = now.unixtime();

  uint32_t diff;
  if (nowUnix < launchTime) {
    diff = launchTime - nowUnix;   // still counting down (L-)
  } else {
    diff = nowUnix - launchTime;   // counting up (T+) or at zero
  }

  long hours = diff / 3600;
  int mins   = (diff % 3600) / 60;
  int secs   = diff % 60;

  // Max out at X999.59.59
  if (hours > 999) {
    hours = 999;
    mins  = 59;
    secs  = 59;
  }

  // Display 1: XHHH.
  disp1Vals[0] = 10;
  disp1Vals[1] = hours / 100;
  if (hours < 100) disp1Vals[1] = 10;
  disp1Vals[2] = hours / 10;
  disp1Vals[3] = hours % 10;
  // Display 2: MM.SS
  disp2Vals[0] = mins / 10;
  disp2Vals[1] = mins % 10;
  disp2Vals[2] = secs / 10;
  disp2Vals[3] = secs % 10;
}

void refreshDisplay1() {
  for (int d = 0; d < 4; d++) {
    int num = disp1Vals[d];
    for (int s = 0; s < 8; s++) {
      byte val = segmentPatterns[num][s];
      if (s == 7 && disp1DP[d]) val = 1;   // force DP on where required
      digitalWrite(segmentPins1[s], val ? HIGH : LOW);
    }
    digitalWrite(digitPins1[d], LOW);      // enable digit (common cathode)
    delayMicroseconds(2000);               // ~2 ms per digit = bright & flicker-free
    digitalWrite(digitPins1[d], HIGH);     // disable digit
  }
}

void refreshDisplay2() {
  for (int d = 0; d < 4; d++) {
    int num = disp2Vals[d];
    for (int s = 0; s < 8; s++) {
      byte val = segmentPatterns[num][s];
      if (s == 7 && disp2DP[d]) val = 1;   // force DP on where required
      digitalWrite(segmentPins2[s], val ? HIGH : LOW);
    }
    digitalWrite(digitPins2[d], LOW);      // enable digit (common cathode)
    delayMicroseconds(2000);
    digitalWrite(digitPins2[d], HIGH);     // disable digit
  }
}

void setup() {
  launchTime = toUnix(L_Zero);
  keypad.addEventListener(keypadEvent);
  // configure LED indicator pins
  pinMode(timeModePin, OUTPUT);
  pinMode(pausedPin, OUTPUT);
  digitalWrite(timeModePin, LOW);
  digitalWrite(pausedPin, LOW);
  // Configure all display pins as outputs
  for (int i = 0; i < 4; i++) {
    pinMode(digitPins1[i], OUTPUT);
    pinMode(digitPins2[i], OUTPUT);
    digitalWrite(digitPins1[i], HIGH);
    digitalWrite(digitPins2[i], HIGH);
  }
  for (int i = 0 ; i < 8; i++) {
    pinMode(segmentPins1[i], OUTPUT);
    pinMode(segmentPins2[i], OUTPUT);
    digitalWrite(segmentPins1[i], LOW);
    digitalWrite(segmentPins2[i], LOW);
  }

  Wire.begin();
  if (!rtc.begin()) {
    // RTC not found – hang here
    while (1) delay(10);
  }
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // ← uncomment ONLY if you need to set the RTC once

  updateDayMode();
  lastUpdate = millis();
}

void loop() {
  // keypad shit
  char key = keypad.getKey();

  // update mode
  if (millis() - lastUpdate >= 1000) {
    findMode();
    lastUpdate = millis();
  }

  refreshDisplay1();
  refreshDisplay2();
}