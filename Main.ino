#include <Adafruit_GFX.h>    
#include <Adafruit_ST7789.h> 
#include <SPI.h>
#include <TMCStepper.h>        
#include <Adafruit_NeoPixel.h> 

// PIN DEFINITIONS

// LCD (ST7789)
#define TFT_CS        5
#define TFT_RST       -1 
#define TFT_DC        2
// Hardware SPI Pins: MOSI=23, SCLK=18

// Buzzer
#define BUZZER_PIN      15  //  Buzzer pin

// Buttons (External Pull-down -> Active HIGH)
#define BTN_RIGHT       35  
#define BTN_LEFT        33  
#define BTN_UP          34  
#define BTN_DOWN        32  
#define BTN_START_STOP  36  
#define BTN_CLEAR       39  
#define BTN_OK          26   
const int buttonPins[] = {BTN_RIGHT, BTN_LEFT, BTN_UP, BTN_DOWN, BTN_START_STOP, BTN_CLEAR, BTN_OK};
const int numButtons = 7;

// Sensors (External Pull-downs -> Active HIGH)
#define LMS_driver_max  21  // End of travel (Extended)
#define LMS_driver_min  14  // Home position (Retracted)
#define syringe_trap    19  // Syringe Clamp Sensor

// Stepper (TMC2209 - UART ONLY)
#define SERIAL_PORT     Serial2
#define RX_PIN          16
#define TX_PIN          17
#define R_SENSE         0.11f 
#define DRIVER_ADDRESS  0b00  

// NeoPixel
#define LED_PIN         25   
#define NUM_LEDS        2    

// OBJECTS
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
TMC2209Stepper driver(&SERIAL_PORT, R_SENSE, DRIVER_ADDRESS);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// CONFIGURATION CONSTANTS & VARIABLES
#define SCREW_LEAD_MM   2.0   
#define STEPS_PER_REV   200   

// Safety Limit (RPM)
#define MAX_RPM_LIMIT   50.0 
// Auto Loading Speed (RPM) 
// Giảm từ 300 xuống 60 để đảm bảo không bị trượt bước - Adjust if the step loss occur
#define HOMING_RPM      35.0 

// Calibration Variables (Editable)
int stepper_mA = 1300;          // Tăng lại lên 1300mA - Back to 1300mA as default
int stepper_microsteps = 16;   
int32_t manual_speed_val = 400; 

float calibrationFactor = 1.0; // Mặc định là 1.0 (100%)

// TMC2209 VACTUAL Calculation (Dynamic)
float vactual_factor = 0.0; 

// STATE MACHINE ENUMS
enum PageState {
  PAGE_HOME,
  PAGE_SETTINGS,
  PAGE_CALIBRATION, 
  PAGE_MANUAL,
  PAGE_RUNNING
};

enum RunningState {
  STATE_IDLE,
  STATE_PUMPING,
  STATE_PAUSED,
  STATE_COMPLETED,
  STATE_ERROR
};

// GLOBAL VARIABLES
PageState currentPage = PAGE_HOME;
RunningState runState = STATE_IDLE;
int menuIndex = 0; 
String errorMessage = ""; // Biến lưu nội dung lỗi - Save error messages

// Syringe Parameters
float syringeDiameter = 20.0; // mm
float targetVolume = 10.0;    // ml
float flowRate = 5.0;         // ml/min

// Settings Edit Step
float editStep = 1.0; 

// Runtime variables
unsigned long pumpStartTime = 0;
unsigned long pumpDuration = 0;  // Expected duration in ms
unsigned long pausedTime = 0;    // To handle pause/resume logic
long targetVactual = 0;          // Speed command sent to TMC
bool redrawRequired = true;

// Debouncing
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;

// FUNCTION PROTOTYPES
void handleInput();
void updateMotorLogic();
void drawScreen();
void printCentered(String text, int y, uint16_t color); 
void startInfusion();
void stopMotor();
void setLedStatus(uint8_t r, uint8_t g, uint8_t b);
void updateMotorConfig();
void beep();
void runHomingSequence(); // Prototype for new function

void setup() {
  Serial.begin(115200);
  
  // 1. Setup Pins
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT); 
  }
  pinMode(LMS_driver_max, INPUT);
  pinMode(LMS_driver_min, INPUT);
  pinMode(syringe_trap, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);     
  digitalWrite(BUZZER_PIN, LOW);   
  
  // 2. Setup NeoPixel
  strip.begin();
  strip.show();
  setLedStatus(0, 0, 50); // Blue on boot

  // 3. Setup TFT
  tft.init(240, 280); 
  tft.setRotation(3); 
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  
  printCentered("Syringe Pump", 40, ST77XX_WHITE);
  printCentered("UART Mode...", 70, ST77XX_WHITE);
  
  beep();

  // 4. Setup TMC2209
  SERIAL_PORT.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  driver.begin();
  driver.toff(4);           
  driver.blank_time(24);
  driver.pwm_autoscale(true);
  
  // Calculate factors before moving
  updateMotorConfig(); 
  
  // RUN AUTO LOADING (HOMING)
  runHomingSequence();

  driver.VACTUAL(0); 
  
  delay(1000);
  redrawRequired = true;
}

void loop() {
  handleInput();       
  updateMotorLogic();  
  drawScreen();     
}

// Buzzer
void beep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50); 
  digitalWrite(BUZZER_PIN, LOW);
}

// Update Motor Settings
void updateMotorConfig() {
  vactual_factor = ((float)STEPS_PER_REV * (float)stepper_microsteps * 16777216.0) / (12000000.0 * 60.0);
  driver.rms_current(stepper_mA);
  driver.microsteps(stepper_microsteps);
  driver.en_spreadCycle(false); // StealthChop (Quiet). Set true if you need more torque!
  driver.pwm_autoscale(true);
}

// Auto Homing / Loading
void runHomingSequence() {
  if (digitalRead(LMS_driver_max) == HIGH) {
    return; 
  }

  // UI Message
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  printCentered("AUTO LOADING...", 80, ST77XX_YELLOW);
  printCentered("Moving to MAX", 110, ST77XX_WHITE);
  
  tft.setTextSize(1);
  printCentered("Press STOP/CLEAR to Abort", 150, ST77XX_RED);

  // Calculate target speed
  long targetSpeed = (long)(HOMING_RPM * vactual_factor);
  
  // SOFT START RAMP
  // Tăng tốc từ từ để tránh rung/trượt bước - Avoid step loss during starting 
  long currentSpeed = 0;
  long stepSize = 1000; // Bước tăng tốc
  
  while(currentSpeed < targetSpeed) {
     currentSpeed += stepSize;
     if(currentSpeed > targetSpeed) currentSpeed = targetSpeed;
     
     driver.VACTUAL(-currentSpeed);
     delay(5); // Delay nhỏ để tạo dốc tăng tốc - Leave it time to accelerate
     
     // Safety check trong lúc tăng tốc
     if (digitalRead(LMS_driver_max) == HIGH) break;
     if (digitalRead(BTN_START_STOP) || digitalRead(BTN_CLEAR)) {
       driver.VACTUAL(0); beep(); return; 
     }
  }

  // Duy trì tốc độ max cho đến khi chạm limit - Remain max speed before hit the limit 
  while (digitalRead(LMS_driver_max) == LOW) {
    // Safety Abort
    if (digitalRead(BTN_START_STOP) || digitalRead(BTN_CLEAR)) {
       driver.VACTUAL(0);
       beep();
       tft.setTextSize(2);
       printCentered("ABORTED!", 180, ST77XX_RED);
       delay(1000);
       return; 
    }
    delay(10); 
  }
  
  // Stop immediately
  driver.VACTUAL(0);
  
  // Success Beeps
  beep(); delay(100); beep();
  tft.fillScreen(ST77XX_BLACK); // Clear for main menu
}

// INPUT HANDLING
void handleInput() {
  if (millis() - lastDebounceTime < debounceDelay) return;

  // 1. START/STOP BUTTON
  if (digitalRead(BTN_START_STOP)) {
    beep(); 
    if (currentPage == PAGE_RUNNING) {
      if (runState == STATE_PUMPING) {
        runState = STATE_PAUSED;
        driver.VACTUAL(0);
        pausedTime = millis(); 
      }
      else if (runState == STATE_PAUSED) {
        runState = STATE_PUMPING;
        pumpStartTime = millis() - (pausedTime - pumpStartTime);
        driver.VACTUAL(targetVactual);
      }
      else if (runState == STATE_ERROR) {
        // Allow exiting error state by stop button
        runState = STATE_IDLE;
        currentPage = PAGE_HOME;
        redrawRequired = true;
      }
    }
    lastDebounceTime = millis();
    redrawRequired = true;
  }
  
  // 2. OK BUTTON
  if (digitalRead(BTN_OK)) {
    beep(); 
    if (currentPage == PAGE_HOME) {
      if (menuIndex == 0) currentPage = PAGE_SETTINGS;
      else if (menuIndex == 1) currentPage = PAGE_CALIBRATION; 
      else if (menuIndex == 2) currentPage = PAGE_MANUAL;
      else if (menuIndex == 3) {
        currentPage = PAGE_RUNNING;
        runState = STATE_IDLE; 
        errorMessage = ""; // Reset error
        startInfusion();
      }
      menuIndex = 0;
    } else if (currentPage == PAGE_SETTINGS) {
       menuIndex++;
       if (menuIndex > 2) { currentPage = PAGE_HOME; menuIndex = 0; }
    } else if (currentPage == PAGE_CALIBRATION) {
       menuIndex++;
       if (menuIndex > 3) { 
         updateMotorConfig();
         currentPage = PAGE_HOME; 
         menuIndex = 0; 
       }
    } else if (currentPage == PAGE_RUNNING && (runState == STATE_COMPLETED || runState == STATE_ERROR)) {
      currentPage = PAGE_HOME; 
    }
    lastDebounceTime = millis();
    redrawRequired = true;
  }

  // 3. CLEAR/BACK BUTTON
  if (digitalRead(BTN_CLEAR)) {
    beep(); 
    if (currentPage == PAGE_RUNNING) {
      stopMotor();
      runState = STATE_IDLE;
      currentPage = PAGE_HOME;
    } else if (currentPage != PAGE_HOME) {
      if (currentPage == PAGE_CALIBRATION) updateMotorConfig(); 
      currentPage = PAGE_HOME;
      menuIndex = 0;
      editStep = 1.0; 
    }
    lastDebounceTime = millis();
    redrawRequired = true;
  }

  // 4. DOWN BUTTON
  if (digitalRead(BTN_DOWN)) {
    beep(); 
    if (currentPage == PAGE_HOME) {
      menuIndex++;
      if (menuIndex > 3) menuIndex = 0;
    } else if (currentPage == PAGE_SETTINGS) {
      if (menuIndex == 0) syringeDiameter -= editStep;
      if (menuIndex == 1) targetVolume -= editStep;
      if (menuIndex == 2) flowRate -= editStep;
      if (syringeDiameter < 0.1) syringeDiameter = 0.1;
      if (targetVolume < 0.1) targetVolume = 0.1;
      if (flowRate < 0.1) flowRate = 0.1;
      
    } else if (currentPage == PAGE_CALIBRATION) {
      if (menuIndex == 0) stepper_mA -= 50;
      if (menuIndex == 1) { if(stepper_microsteps > 1) stepper_microsteps /= 2; }
      if (menuIndex == 2) manual_speed_val -= 50;
      if (menuIndex == 3) {
        calibrationFactor -= 0.01;
        if (calibrationFactor < 0.1) calibrationFactor = 0.1;
      }
      updateMotorConfig(); 
    }
    lastDebounceTime = millis();
    redrawRequired = true;
  }

  // 5. UP BUTTON
  if (digitalRead(BTN_UP)) {
    beep(); 
    if (currentPage == PAGE_HOME) {
      menuIndex--;
      if (menuIndex < 0) menuIndex = 3;
    } else if (currentPage == PAGE_SETTINGS) {
      if (menuIndex == 0) syringeDiameter += editStep;
      if (menuIndex == 1) targetVolume += editStep;
      if (menuIndex == 2) flowRate += editStep;
    } else if (currentPage == PAGE_CALIBRATION) {
      if (menuIndex == 0) stepper_mA += 50;
      if (menuIndex == 1) { if(stepper_microsteps < 256) stepper_microsteps *= 2; }
      if (menuIndex == 2) manual_speed_val += 50;
      if (menuIndex == 3) {
        calibrationFactor += 0.01;
        if (calibrationFactor > 5.0) calibrationFactor = 5.0;
      }
      updateMotorConfig(); 
    }
    lastDebounceTime = millis();
    redrawRequired = true;
  }

  // 7. LEFT BUTTON
  if (digitalRead(BTN_LEFT)) {
    if (currentPage == PAGE_SETTINGS) {
       if (editStep < 99.0) editStep *= 10.0;
       if (editStep > 9.0 && editStep < 11.0) editStep = 10.0;
       if (editStep > 90.0) editStep = 100.0;
       beep();
       lastDebounceTime = millis();
       redrawRequired = true;
    }
  }

  // 8. RIGHT BUTTON
  if (digitalRead(BTN_RIGHT)) {
    if (currentPage == PAGE_SETTINGS) {
       if (editStep > 0.11) editStep /= 10.0;
       if (editStep < 0.11) editStep = 0.1;
       else if (editStep > 0.9 && editStep < 1.1) editStep = 1.0;
       beep();
       lastDebounceTime = millis();
       redrawRequired = true;
    }
  }
  
  // 6. MANUAL JOG
  if (currentPage == PAGE_MANUAL) {
    if (digitalRead(BTN_RIGHT)) {
      if (digitalRead(LMS_driver_max) == LOW) driver.VACTUAL(-manual_speed_val);
      else driver.VACTUAL(0);
    } else if (digitalRead(BTN_LEFT)) {
      if (digitalRead(LMS_driver_min) == LOW) driver.VACTUAL(manual_speed_val);
      else driver.VACTUAL(0);
    } else {
      driver.VACTUAL(0);
    }
  }
}

// LOGIC FUNCTIONS
void startInfusion() {
  float radius = syringeDiameter / 2.0;
  float area = PI * (radius * radius);
  float linearSpeed_mm_min = (flowRate * 1000.0) / area;
  float rpm = linearSpeed_mm_min / SCREW_LEAD_MM;
  
  // SAFETY CHECK
  if (rpm > MAX_RPM_LIMIT) {
    runState = STATE_ERROR;
    errorMessage = "SPEED TOO HIGH!";
    setLedStatus(255, 0, 0); // Red LED
    
    // Alert Beeps
    beep(); delay(100); beep(); delay(100); beep();
    
    return; // STOP HERE, DO NOT MOVE MOTOR
  }
  
  targetVactual = (long)(rpm * vactual_factor * calibrationFactor);
  
  float durationMin = targetVolume / flowRate;
  pumpDuration = (unsigned long)(durationMin * 60.0 * 1000.0);
  
  pumpStartTime = millis();
  driver.VACTUAL(targetVactual);

  runState = STATE_PUMPING;
  setLedStatus(255, 100, 0); 
}

void stopMotor() {
  driver.VACTUAL(0);
}

void updateMotorLogic() {
  if (digitalRead(LMS_driver_max) == HIGH) {
     driver.VACTUAL(0);
     if (runState == STATE_PUMPING) {
       runState = STATE_ERROR;
       errorMessage = "END LIMIT HIT";
       setLedStatus(255, 0, 0);
     }
  }
  
  if (currentPage == PAGE_RUNNING && runState == STATE_PUMPING) {
    unsigned long elapsed = millis() - pumpStartTime;
    if (elapsed >= pumpDuration) {
      stopMotor();
      runState = STATE_COMPLETED;
      setLedStatus(0, 255, 0); 
      redrawRequired = true;
      beep(); beep(); 
    }
    static unsigned long lastGuiUpdate = 0;
    if (millis() - lastGuiUpdate > 500) {
      redrawRequired = true;
      lastGuiUpdate = millis();
    }
  }
}

// FUNCTION TO CENTER TEXT 
void printCentered(String text, int y, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int x = (tft.width() - w) / 2;
  if (x < 0) x = 0; 
  
  tft.setCursor(x, y);
  tft.setTextColor(color);
  tft.print(text);
}

// DISPLAY LOGIC
void drawScreen() {
  if (!redrawRequired) return;

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2); 
  
  int yTitle = 5;
  int yLine1 = 35;
  int yLine2 = 60;
  int yLine3 = 85;
  int yLine4 = 110;
  int yFooter = 135; 

  String buf; 

  switch (currentPage) {
    case PAGE_HOME:
      printCentered("== MAIN MENU ==", yTitle, ST77XX_WHITE);
      
      buf = (menuIndex == 0) ? "> Settings <" : "Settings";
      printCentered(buf, yLine1, (menuIndex == 0 ? ST77XX_GREEN : ST77XX_WHITE));

      buf = (menuIndex == 1) ? "> Calibration <" : "Calibration";
      printCentered(buf, yLine2, (menuIndex == 1 ? ST77XX_GREEN : ST77XX_WHITE));
      
      buf = (menuIndex == 2) ? "> Manual/Load <" : "Manual/Load";
      printCentered(buf, yLine3, (menuIndex == 2 ? ST77XX_GREEN : ST77XX_WHITE));
      
      buf = (menuIndex == 3) ? "> START RUN <" : "START RUN";
      printCentered(buf, yLine4, (menuIndex == 3 ? ST77XX_GREEN : ST77XX_WHITE));
      
      tft.setTextSize(1);
      if(digitalRead(syringe_trap)) {
         printCentered("WARN: Clamp Open", yFooter+10, ST77XX_RED);
      } else {
         printCentered("Status: Ready", yFooter+10, ST77XX_GREEN);
      }
      break;

    case PAGE_SETTINGS:
      printCentered("== SETTINGS ==", yTitle, ST77XX_WHITE);
      
      buf = "Dia: " + String(syringeDiameter, 1) + "mm";
      printCentered(buf, yLine1, (menuIndex == 0 ? ST77XX_GREEN : ST77XX_WHITE));

      buf = "Vol: " + String(targetVolume, 1) + "ml";
      printCentered(buf, yLine2, (menuIndex == 1 ? ST77XX_GREEN : ST77XX_WHITE));

      buf = "Rate: " + String(flowRate, 1) + "ml/m";
      printCentered(buf, yLine3, (menuIndex == 2 ? ST77XX_GREEN : ST77XX_WHITE));
      
      tft.setTextSize(1);
      buf = "[Step: " + String(editStep, 1) + "] L:x10 R:/10";
      printCentered(buf, yFooter, ST77XX_YELLOW);
      
      printCentered("OK: Next", yFooter + 12, ST77XX_WHITE);
      break;

    case PAGE_CALIBRATION:
      printCentered("== CALIB ==", yTitle, ST77XX_WHITE);
      
      buf = "I_rms: " + String(stepper_mA) + "mA";
      printCentered(buf, yLine1, (menuIndex == 0 ? ST77XX_GREEN : ST77XX_WHITE));

      buf = "Step: " + String(stepper_microsteps);
      printCentered(buf, yLine2, (menuIndex == 1 ? ST77XX_GREEN : ST77XX_WHITE));

      buf = "Jog: " + String(manual_speed_val);
      printCentered(buf, yLine3, (menuIndex == 2 ? ST77XX_GREEN : ST77XX_WHITE));

      buf = "Scale: " + String(calibrationFactor, 2); 
      printCentered(buf, 110, (menuIndex == 3 ? ST77XX_GREEN : ST77XX_WHITE));
      
      tft.setTextSize(1);
      printCentered("OK: Save/Exit", yFooter, ST77XX_WHITE);
      break;

    case PAGE_MANUAL:
      printCentered("== MANUAL ==", yTitle, ST77XX_WHITE);
      
      printCentered("Hold R: Push ->", yLine1, ST77XX_WHITE);
      printCentered("Hold L: Pull <-", yLine2, ST77XX_WHITE);
      
      if (digitalRead(BTN_RIGHT)) printCentered(">> PUSHING >>", yLine3+10, ST77XX_GREEN);
      else if (digitalRead(BTN_LEFT)) printCentered("<< PULLING <<", yLine3+10, ST77XX_GREEN);
      else printCentered("IDLE", yLine3+10, ST77XX_WHITE);
      break;

    case PAGE_RUNNING:
      if (runState == STATE_ERROR) {
        // RED WARNING SCREEN
        tft.fillScreen(ST77XX_RED);
        printCentered("!!! WARNING !!!", yTitle + 10, ST77XX_WHITE);
        printCentered(errorMessage, yLine2, ST77XX_WHITE);
        
        tft.setTextSize(1);
        printCentered("Check Settings/Limits", yLine3 + 10, ST77XX_WHITE);
        printCentered("Press CLEAR to Exit", yFooter, ST77XX_YELLOW);
        
      } else {
        // Normal Running Screen
        printCentered("== RUNNING ==", yTitle, ST77XX_WHITE);
        
        String stateStr = "State: ";
        if (runState == STATE_PUMPING) stateStr += "PUMPING";
        else if (runState == STATE_PAUSED) stateStr += "PAUSED";
        else if (runState == STATE_COMPLETED) stateStr += "DONE";
        else stateStr += "WAIT";
        printCentered(stateStr, yLine1, ST77XX_WHITE);
  
        unsigned long elapsed = 0;
        if (runState != STATE_IDLE) {
           elapsed = millis() - pumpStartTime;
           if (elapsed > pumpDuration) elapsed = pumpDuration;
        }
        float volDone = ((float)elapsed / (float)pumpDuration) * targetVolume;
        float volRem = targetVolume - volDone;
        if (volRem < 0) volRem = 0;
        
        buf = "Done: " + String(volDone, 1) + " ml";
        printCentered(buf, yLine2, ST77XX_WHITE);
  
        buf = "Rem: " + String(volRem, 1) + " ml";
        printCentered(buf, yLine3, ST77XX_WHITE);
        
        tft.setTextSize(1);
        printCentered("START: P/R | CLEAR: Stop", yFooter, ST77XX_WHITE);
      }
      break;
  }
  
  redrawRequired = false;
}

void setLedStatus(uint8_t r, uint8_t g, uint8_t b) {
  for(int i=0; i<NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}


