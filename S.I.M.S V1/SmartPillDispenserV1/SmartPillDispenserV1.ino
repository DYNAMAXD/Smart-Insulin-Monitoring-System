#include <7semi_HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "time.h"

const int dataPin = 19;
const int clockPin = 18;
float scale_factor = 230.0;
HX711_7semi loadcell(dataPin, clockPin);

LiquidCrystal_I2C lcd(0x27,16,2); // Try 0x3F if 0x27 doesn't work

struct Dose {
  int hour;
  int minute;
  float ml;
  float expectedWeight;
};

#define DOSE_COUNT 3
Dose doses[DOSE_COUNT] = {
  {15, 0, 2.0, 5.0},
  {15, 5, 5.0, 12.0},
  {15,10,2.0, 5.0}
};

bool doseActive = false;
Dose currentDose;
float weightBefore = 0;
bool vialTaken = false;
unsigned long vialReplacedTime = 0;
unsigned long helpTimerStart = 0;
const unsigned long helpDuration = 5*60*1000;

String upcomingText = "";
int rollIndex = 0;
unsigned long lastRollUpdate = 0;
const unsigned long rollInterval = 500;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;
const int daylightOffset_sec = 0;

bool timeSynced = false;

void setup() {
  Serial.begin(115200);
  
  // Initialize LCD with retry logic
  Wire.begin(21, 20); // SDA, SCL
  lcd.init();
  lcd.backlight();
  
  // Show startup message
  lcd.setCursor(0,0);
  lcd.print("Medicine Monitor");
  lcd.setCursor(0,1);
  lcd.print("Starting...     ");
  delay(2000);

  // Initialize load cell
  loadcell.begin();
  loadcell.setScale(scale_factor);
  loadcell.tare();

  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Waiting for");
  lcd.setCursor(0,1);
  lcd.print("time sync...   ");
}

void loop() {
  unsigned long now = millis();
  float currentWeight = loadcell.getWeight();

  // Serial debug commands
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.equalsIgnoreCase("w")) {
      Serial.printf("Current weight: %.2f g\n", currentWeight);
    } else if (input.equalsIgnoreCase("t")) {
      loadcell.tare();
      Serial.println("Tare performed");
    }
  }

  // Get time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    if (!timeSynced) {
      // Still waiting for time sync
      if (now % 2000 < 100) { // Blink to show it's alive
        lcd.setCursor(0,0);
        lcd.print("Waiting for    ");
        lcd.setCursor(0,1);
        lcd.print("time sync...   ");
      }
    }
    return; // Don't proceed without time
  } else if (!timeSynced) {
    timeSynced = true;
    lcd.clear();
    Serial.println("Time synchronized!");
  }

  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  int hour12 = (currentHour % 12 == 0) ? 12 : currentHour % 12;
  char meridian = (currentHour < 12) ? 'A' : 'P';

  // Display current time on top row
  lcd.setCursor(0,0);
  lcd.print("Time: ");
  lcd.print(hour12);
  lcd.print(":");
  if (currentMinute < 10) lcd.print("0");
  lcd.print(currentMinute);
  lcd.print(" ");
  lcd.print(meridian);
  lcd.print("M   ");

  // Check for dose times
  if (!doseActive) {
    for (int i = 0; i < DOSE_COUNT; i++) {
      if (currentHour == doses[i].hour && currentMinute == doses[i].minute) {
        doseActive = true;
        currentDose = doses[i];
        vialTaken = false;
        vialReplacedTime = 0;
        helpTimerStart = 0;
        weightBefore = currentWeight;
        
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Dose Time!     ");
        lcd.setCursor(0,1);
        lcd.print("Take ");
        lcd.print(currentDose.ml, 1);
        lcd.print("ml     ");
        
        Serial.printf("[DOSE] %0.1fml at %d:%02d\n", currentDose.ml, currentHour, currentMinute);
        break;
      }
    }
  }

  // Dose monitoring logic
  if (doseActive) {
    // Vial pickup detection
    if (!vialTaken && currentWeight < weightBefore - 1.0) {
      vialTaken = true;
      helpTimerStart = now;
      
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Dose: ");
      lcd.print(currentDose.ml, 1);
      lcd.print("ml    ");
      lcd.setCursor(0,1);
      lcd.print("Replace vial   ");
      
      Serial.println("Vial taken");
    }

    // Vial replace and dose completion
    if (vialTaken && vialReplacedTime == 0 && currentWeight >= weightBefore - 0.5) {
      vialReplacedTime = now;
      Serial.println("Vial replaced");
    }

    if (vialReplacedTime > 0 && now - vialReplacedTime >= 3000) {
      // Dose completed
      float doseTaken = weightBefore - currentWeight;
      
      lcd.clear();
      lcd.setCursor(0,0);
      if (abs(doseTaken - currentDose.expectedWeight) < 1.0) {
        lcd.print("Dose Complete! ");
        Serial.println("Dose taken correctly");
      } else {
        lcd.print("Check Dose!    ");
        Serial.println("Dose may be incorrect");
      }
      lcd.setCursor(0,1);
      lcd.print("Weight: ");
      lcd.print(doseTaken, 1);
      lcd.print("g    ");
      
      delay(3000);
      doseActive = false;
      lcd.clear();
    }

    // Help timer countdown
    if (vialTaken && vialReplacedTime == 0) {
      unsigned long elapsed = now - helpTimerStart;
      if (elapsed < helpDuration) {
        int secondsLeft = (helpDuration - elapsed) / 1000;
        lcd.setCursor(0,1);
        lcd.print("Help in ");
        lcd.print(secondsLeft);
        lcd.print("s    ");
      } else {
        lcd.setCursor(0,1);
        lcd.print("HELP ALERT!    ");
      }
    }
  } else {
    // Normal operation - show upcoming doses
    if (now - lastRollUpdate >= rollInterval) {
      lastRollUpdate = now;
      
      // Build upcoming doses text
      upcomingText = "Upcoming: ";
      for (int i = 0; i < DOSE_COUNT; i++) {
        int displayHour = doses[i].hour % 12;
        if (displayHour == 0) displayHour = 12;
        upcomingText += String(displayHour) + ":";
        if (doses[i].minute < 10) upcomingText += "0";
        upcomingText += String(doses[i].minute);
        upcomingText += String("(") + String(doses[i].ml, 1) + "ml) ";
      }
      
      // Scroll text
      String displayText = upcomingText.substring(rollIndex);
      if (displayText.length() < 16) {
        displayText += upcomingText.substring(0, 16 - displayText.length());
      } else {
        displayText = displayText.substring(0, 16);
      }
      
      lcd.setCursor(0,1);
      lcd.print(displayText);
      
      rollIndex++;
      if (rollIndex >= upcomingText.length()) {
        rollIndex = 0;
      }
    }
  }

  // Auto-tare when weight is very low
  if (currentWeight < 0.5 && !doseActive) {
    loadcell.tare();
    Serial.println("Auto-tare performed");
    delay(100);
  }
}