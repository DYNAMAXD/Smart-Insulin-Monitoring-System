#include <HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include "time.h"

// HX711
const int dataPin = 19;
const int clockPin = 18;
float scale_factor = 230.0;
HX711 loadcell;

// LCD
LiquidCrystal_I2C lcd(0x27,16,2);
#define SDA_PIN 21
#define SCL_PIN 20

// WiFi credentials
const char* ssid     = "wifi1";
const char* password = "12345678";

// NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;  // IST
const int daylightOffset_sec = 0;

// Dose schedule
struct Dose {
  int hour;
  int minute;
  float ml;
  float expectedWeight;
};

#define DOSE_COUNT 5
Dose doses[DOSE_COUNT] = {
  {15, 0, 2.0, 5.0},  // 3:00 PM
  {15, 5, 5.0, 12.0}, // 3:05 PM
  {15,10,2.0, 5.0} ,   // 3:10 PM
  {20,45,2.0, 5.0} ,   // 8:30 PM
  {11,9,2.0, 5.0}    // 8:30 PM
};

// Dose monitoring variables
bool doseActive = false;
Dose currentDose;
float initialVialWeight = 0;    // Weight of vial with medicine
float finalVialWeight = 0;      // Weight after medicine is taken
bool vialTaken = false;
unsigned long vialReplacedTime = 0;
unsigned long doseStartTime = 0;
const unsigned long helpDuration = 5*60*1000; // 5 minutes

// Auto tare
unsigned long stableStart = 0;
bool inStableRange = false;
float lastStableWeight = 0;

// Timing
unsigned long lastLCDupdate = 0;
const unsigned long lcdInterval = 300; // Faster updates for smoother animation

// Smooth scrolling variables
String upcomingText = "";
int scrollPosition = 0;
unsigned long lastScrollUpdate = 0;
const unsigned long scrollInterval = 180; // Smoother scrolling speed
const int scrollGap = 3; // Gap between repetitions
String extendedText = ""; // Store extended text

// WiFi state
bool wifiConnected = false;
bool timeSynced = false;

// For background WiFi connect
unsigned long lastWiFiAttempt = 0;
const unsigned long wifiRetryInterval = 5000;

// Prevent double dose triggering
unsigned long lastDoseTrigger = 0;
const unsigned long doseCooldown = 60000; // 1 minute cooldown between doses

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN,SCL_PIN);
  lcd.init();
  lcd.backlight();

  // HX711 setup
  loadcell.begin(dataPin, clockPin);
  loadcell.set_scale(scale_factor);
  loadcell.tare(); // Initial tare
  // Don't tare at startup - wait for stable reading
  delay(1000);
  
  // Initial splash screen
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Welcome!");
  lcd.setCursor(0,1);
  lcd.print("Medicine Monitor");
  delay(2000);

  // Build initial scrolling text
  buildExtendedText();
  
  // Show home screen immediately
  lcd.clear();
  displayHomeScreen();
  
  // Start WiFi (non-blocking)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid,password);
  lastWiFiAttempt = millis();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

String get12HourTime(int hour, int minute) {
  int hour12 = (hour % 12 == 0) ? 12 : hour % 12;
  String period = (hour < 12) ? "AM" : "PM";
  String timeStr = String(hour12) + ":";
  if (minute < 10) timeStr += "0";
  timeStr += String(minute) + period;
  return timeStr;
}

String buildUpcomingDosesText() {
  String text = "";
  for (int i = 0; i < DOSE_COUNT; i++) {
    text += get12HourTime(doses[i].hour, doses[i].minute);
    text += "(" + String(doses[i].ml,1) + "ml) ";
  }
  return text;
}

void buildExtendedText() {
  upcomingText = buildUpcomingDosesText();
  
  if (upcomingText.length() <= 16) {
    extendedText = upcomingText;
  } else {
    // Manually create the gap spaces (fix for no repeat() method)
    String gap = "";
    for (int i = 0; i < scrollGap; i++) {
      gap += " ";
    }
    extendedText = upcomingText + gap + upcomingText;
  }
}

String getSmoothScrollingText() {
  if (extendedText.length() <= 16) {
    return extendedText;
  }
  
  // Extract the visible portion
  String visible = extendedText.substring(scrollPosition, scrollPosition + 16);
  
  // Ensure we always return exactly 16 characters
  if (visible.length() < 16) {
    visible += extendedText.substring(0, 16 - visible.length());
  }
  
  return visible;
}

void checkDoseStart() {
  if (doseActive) return;
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  
  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  unsigned long now = millis();
  
  // Prevent double triggering with cooldown
  if (now - lastDoseTrigger < doseCooldown) return;
  
  for (int i = 0; i < DOSE_COUNT; i++) {
    if (currentHour == doses[i].hour && currentMinute == doses[i].minute) {
      doseActive = true;
      currentDose = doses[i];
      initialVialWeight = loadcell.get_units(); // Record starting weight
      vialTaken = false;
      vialReplacedTime = 0;
      doseStartTime = now;
      lastDoseTrigger = now;
      
      Serial.printf("[DOSE START] %0.1fml at %s\n", currentDose.ml, get12HourTime(currentHour, currentMinute).c_str());
      Serial.printf("[INITIAL WEIGHT] %.2f g\n", initialVialWeight);
      break;
    }
  }
}

void monitorDose() {
  if (!doseActive) return;
  
  unsigned long now = millis();
  float currentWeight = loadcell.get_units();
  
  // Vial taken detection - weight drops significantly (vial lifted)
  if (!vialTaken && currentWeight < initialVialWeight - 2.0) { // Weight drops by at least 2g
    vialTaken = true;
    Serial.println("[VIAL TAKEN] Vial removed from scale");
    Serial.printf("[WEIGHT WHEN TAKEN] %.2f g\n", currentWeight);
  }
  
  // Vial replaced detection - FIXED: Better logic
  if (vialTaken && vialReplacedTime == 0) {
    // Check if weight has returned to a stable value (not zero)
    if (currentWeight > 10.0 && abs(currentWeight - lastStableWeight) < 0.5) {
      if (!inStableRange) {
        inStableRange = true;
        stableStart = now;
      } else if (now - stableStart >= 1000) { // Stable for 1 second
        vialReplacedTime = now;
        finalVialWeight = currentWeight;
        
        // Calculate actual medicine taken - FIXED LOGIC
        float medicineTaken = initialVialWeight - finalVialWeight;
        
        Serial.printf("[VIAL REPLACED] Final weight: %.2f g\n", finalVialWeight);
        Serial.printf("[MEDICINE TAKEN] Expected: %.1fg, Actual: %.1fg\n", currentDose.expectedWeight, medicineTaken);
        
        // Check if correct amount was taken
        if (abs(medicineTaken - currentDose.expectedWeight) <= 1.0) {
          Serial.println("[RESULT] Correct dose taken! ✓");
        } else if (medicineTaken < currentDose.expectedWeight - 1.0) {
          Serial.println("[RESULT] Dose too small! ✗");
        } else {
          Serial.println("[RESULT] Dose too large! ✗");
        }
        inStableRange = false;
      }
    } else {
      inStableRange = false;
      lastStableWeight = currentWeight;
    }
  }
  
  // Auto-complete dose after 5 seconds of vial replacement
  if (vialReplacedTime > 0 && now - vialReplacedTime >= 5000) {
    doseActive = false;
    Serial.println("[DOSE COMPLETE] Monitoring ended");
  }
  
  // Auto-cancel dose if help timer expires without interaction
  if (!vialTaken && (now - doseStartTime) >= helpDuration) {
    doseActive = false;
    Serial.println("[DOSE CANCELLED] No interaction within 5 minutes");
  }
}

void displayDoseScreen() {
  if (!doseActive) return;
  
  unsigned long now = millis();
  unsigned long timeElapsed = now - doseStartTime;
  int secondsLeft = (helpDuration - timeElapsed) / 1000;
  if (secondsLeft < 0) secondsLeft = 0;
  
  // Clear both lines first
  lcd.setCursor(0,0);
  lcd.print("                ");
  lcd.setCursor(0,1);
  lcd.print("                ");
  
  if (!vialTaken) {
    // First line: Take medicine instruction
    lcd.setCursor(0,0);
    lcd.print("Take ");
    lcd.print(currentDose.ml, 1);
    lcd.print("ml medicine");
    
    // Second line: Help timer
    lcd.setCursor(0,1);
    lcd.print("Help ");
    lcd.print(secondsLeft / 60);
    lcd.print(":");
    if ((secondsLeft % 60) < 10) lcd.print("0");
    lcd.print(secondsLeft % 60);
    
  } else if (vialReplacedTime == 0) {
    // First line: Return vial instruction
    lcd.setCursor(0,0);
    lcd.print("Place vial back");
    
    // Second line: Help timer continues
    lcd.setCursor(0,1);
    lcd.print("Help ");
    lcd.print(secondsLeft / 60);
    lcd.print(":");
    if ((secondsLeft % 60) < 10) lcd.print("0");
    lcd.print(secondsLeft % 60);
    
  } else {
    // Show result after vial is returned
    float medicineTaken = initialVialWeight - finalVialWeight;
    
    // First line: Result message
    lcd.setCursor(0,0);
    if (abs(medicineTaken - currentDose.expectedWeight) <= 1.0) {
      lcd.print("Dose correct!   ");
    } else {
      lcd.print("Check dose!     ");
    }
    
    // Second line: Actual amount taken
    lcd.setCursor(0,1);
    lcd.print("Taken:");
    lcd.print(medicineTaken, 1);
    lcd.print("g");
    
    // Add spaces to clear any remaining characters
    int spacesNeeded = 16 - (6 + String(medicineTaken,1).length() + 1);
    for (int i = 0; i < spacesNeeded; i++) {
      lcd.print(" ");
    }
  }
}

void displayHomeScreen() {
  struct tm timeinfo;
  bool timeAvailable = getLocalTime(&timeinfo);
  
  // First line: Empty when no WiFi, Time when connected
  lcd.setCursor(0,0);
  if (wifiConnected && timeAvailable) {
    int hour12 = (timeinfo.tm_hour % 12 == 0) ? 12 : timeinfo.tm_hour % 12;
    lcd.print("    ");
    lcd.print(hour12);
    lcd.print(":");
    if (timeinfo.tm_min < 10) lcd.print("0");
    lcd.print(timeinfo.tm_min);
    lcd.print((timeinfo.tm_hour < 12) ? "AM" : "PM");
    lcd.print("     ");
  } else {
    lcd.print("                ");
  }

  // Second line: Always show scrolling doses
  lcd.setCursor(0,1);
  if (upcomingText.length() > 0) {
    lcd.print(getSmoothScrollingText());
  } else {
    lcd.print("Connect WiFi... ");
  }
}

void loop() {
  unsigned long now = millis();

  // Background WiFi connect
  if (!wifiConnected) {
    if (now - lastWiFiAttempt >= wifiRetryInterval) {
      lastWiFiAttempt = now;
      if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(ssid, password);
      } else {
        wifiConnected = true;
        Serial.print("WiFi connected! IP: ");
        Serial.println(WiFi.localIP());
        buildExtendedText(); // Rebuild text with actual times
      }
    }
  } else if (!timeSynced) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      timeSynced = true;
      Serial.println("Time synchronized!");
      buildExtendedText(); // Rebuild text with synced times
    }
  }

  // Serial monitor commands
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.equalsIgnoreCase("w")) {
      float weight = loadcell.get_units();
      Serial.printf("Current weight: %.2f g\n", weight);
    } else if (input.equalsIgnoreCase("t")) {
      loadcell.tare();
      Serial.println("Manual tare performed");
    } else if (input.equalsIgnoreCase("d")) {
      // Debug command to check dose status
      Serial.printf("Dose Active: %s\n", doseActive ? "YES" : "NO");
      Serial.printf("Vial Taken: %s\n", vialTaken ? "YES" : "NO");
      Serial.printf("Vial Replaced: %s\n", vialReplacedTime > 0 ? "YES" : "NO");
      Serial.printf("Initial Weight: %.2f g\n", initialVialWeight);
      Serial.printf("Current Weight: %.2f g\n", loadcell.get_units());
    }
  }

  // Auto tare logic (only when no dose active)
  float weight = loadcell.get_units();
  if (!doseActive) {
    // Check if weight is stable (within 0.5g for 2 seconds)
    if (abs(weight - lastStableWeight) < 0.5) {
      if (!inStableRange) {
        inStableRange = true;
        stableStart = now;
      } else if (now - stableStart >= 2000) { // Wait 2 seconds of stability
        // Tare if weight is negative OR near zero
        if (weight < -1.0 || (weight >= -1.0 && weight < 2.0)) {
          loadcell.tare();
          Serial.printf("[AUTO TARE] Performed - weight was %.2f g\n", weight);
          inStableRange = false;
        }
      }
    } else {
      inStableRange = false;
      lastStableWeight = weight;
    }
  }

  // Dose management
  checkDoseStart();
  monitorDose();

  // LCD update
  if (now - lastLCDupdate >= lcdInterval) {
    lastLCDupdate = now;
    
    if (doseActive) {
      displayDoseScreen();
    } else {
      displayHomeScreen();
    }
  }

  // Smooth scrolling update (independent of LCD update)
  if (!doseActive && extendedText.length() > 16 && now - lastScrollUpdate >= scrollInterval) {
    lastScrollUpdate = now;
    
    scrollPosition++;
    
    // Reset position when we've scrolled through one complete cycle
    if (scrollPosition >= extendedText.length() - 16) {
      scrollPosition = 0;
    }
  }
}