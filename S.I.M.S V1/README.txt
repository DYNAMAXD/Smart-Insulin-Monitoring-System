Medicine Monitor - V1 (Basic Version) 
Time-based Dose Reminders: Pre-scheduled medication alerts

Weight-based Verification: HX711 load cell detects when medicine vial is taken/replaced

LCD Display: Real-time status and upcoming doses

Automatic Time Sync: NTP time synchronization

Help Alert System: Timer that triggers help alert if vial isn't replaced

Serial Debug Interface: Manual weight reading and tare commands

Hardware Components
Microcontroller: ESP32-S2

Weight Sensor: HX711 Load Cell Amplifier

Display: 16x2 I2C LCD (0x27 address)

Connections:
HX711: DATA=GPIO19, CLK=GPIO18
LCD: SDA=GPIO21, SCL=GPIO20

Configuration
Dose Schedule
cpp
Dose doses[DOSE_COUNT] = {
  {15, 0, 2.0, 5.0},   // 3:00 PM - 2.0ml (expected 5.0g)
  {15, 5, 5.0, 12.0},  // 3:05 PM - 5.0ml (expected 12.0g)
  {15,10,2.0, 5.0}     // 3:10 PM - 2.0ml (expected 5.0g)
};
Time Settings
NTP Server: pool.ntp.org
GMT Offset: +5:30 (19800 seconds - Indian Standard Time)
Daylight Saving: 0 seconds

Code Structure
Libraries: Custom HX711, LiquidCrystal_I2C, Time, Wire
Main States: Time sync, Dose monitoring, Normal operation
Key Variables: Dose tracking, weight monitoring, timer management

Software Setup:
Select "ESP32S2 Dev Module" in Arduino IDE
 
Calibration:

Adjust scale_factor (currently 230.0) for accurate weight readings
Set expected weights in dose array based on actual medicine weight

Usage
Normal Operation:
Display shows current time and scrolling upcoming doses

Automatic tare when weight < 0.5g

Dose Time:
Alert: "Dose Time! Take X.Xml"

Vial Detection: Weight drop triggers monitoring

Replacement: Vial return starts completion timer

Verification: Compares actual vs expected weight

Serial Commands:
w - Read current weight

t - Manual tare
 
Help Timer: 5-minute alert if vial not replaced

Weight Verification: Checks if correct amount was taken

Auto-tare: Prevents drift in weight readings



but there are some Limitations with V1
No WiFi connectivity for remote monitoring

No data logging or history

Fixed dose schedule in code

No battery backup support

Manual calibration required