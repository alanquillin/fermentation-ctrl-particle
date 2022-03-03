/*
 * Project fermentation-controller
 * Description:
 * Author: Alan Quillin
 */

#include <LiquidCrystal_I2C_Spark.h>
#include <DS18B20.h>
#include <math.h>
#include <service.h>

//func prototypes
void refreshDisplay(bool clear=false);

//pin definitions
const int16_t P_DS_DATA   = D2;
const int16_t P_BTN_UP    = A0;
const int16_t P_BTN_DOWN  = A1;
const int16_t P_BTN_SET   = A2;
const int16_t P_CTRL_COOL = D3;
const int16_t P_CTRL_HEAT = D4;

//charactors
const uint8_t CHAR_SPACE  = 0x20;
const uint8_t CHAR_ARROW  = 0x7E;
const uint8_t CHAR_DEGREE = 0xDF;

// modes
const int MODE_COOLING = 1;
const int MODE_HEATING = 2;
const int MODE_HOLD    = 3;
const int MODE_OFF     = 0;

// states
const int STATE_DEFAULT            = 0;
const int STATE_MENU               = 1;
const int STATE_MENU_SET_TEMP      = 2;
const int STATE_MENU_CALIBRATE     = 3;
const int STATE_MENU_SET_PRECISION = 4;
const int STATE_MENU_TEMP_VAR      = 5;

// other constants
const int MAXRETRY = 4;
const int LONG_BTN_PRESS_MILS = 2000;

// dataservice
const String HOST = "<__IP_Address__>";
const int PORT = 80;

LiquidCrystal_I2C lcd(0x27, 20, 4);
DS18B20 ds18b20(P_DS_DATA, true);
DataService dataService(HOST, PORT);
String manufacturerId;
String deviceId;

double fahrenheit = 0;
double targetTemp = 68;
double tTargetTemp = targetTemp;
double tempVariable = 1;
double tTempVariable = tempVariable;
double precision = .5;
double tPrecision = precision;
double calibrationDiff = 0;
double calibrationTemp;
int currentMode = MODE_HOLD;
int displayState = STATE_DEFAULT;
bool refreshLock = false;
uint32_t msLastSample;

// menu state vars
const String MENU_SET_MODE_OPTION = "<SET MODE>";
const int MENU_ITEM_SIZE = 4;
const String MENU_ITEMS[MENU_ITEM_SIZE] = {
  "Set Target Temp   ",
  MENU_SET_MODE_OPTION,
  "Calibrate Temp    ",
  "Back              "
};
int menuFirstItemIndex = 0;
int menuItemIndex = 0;

// button state vars
int btnUpState = LOW;
uint32_t btnUpPressStart = 0;
bool btnUpLongPressedHandled = false;
int btnDownState = LOW;
uint32_t btnDownPressStart = 0;
bool btnDownLongPressedHandled = false;
int btnSetState = LOW;
uint32_t btnSetPressStart = 0;
bool btnSetLongPressedHandled = false;

// Timers
Timer tempTimer(2000, getTemp);
Timer refreshDisplayTimer(1000, refreshDisplayWrapper);
Timer setModeTimer(1000, setModeCtl);
Timer pingServiceTimer(120000, ping);

// setup() runs once, when the device is first turned on.
void setup() {
  // Put initialization like pinMode and begin functions here.
  Serial.begin(9600);
  Serial.println("Starting up...");
  lcd.init();
  lcd.backlight();
  lcd.clear();
  Serial.println("Initializing...");
  lcd.print("Initializing...");
  
  manufacturerId = System.deviceID();

  Serial.printlnf("Device Manufacturer Id: %s", manufacturerId);
  Serial.printlnf("Wifi MAC address: %s", WiFi.macAddress());
  Serial.printlnf("Wifi IP address: %s", WiFi.localIP());

  lcd.setCursor(0,1);
  lcd.print("Wifi... %s" WiFi.ready() ? "connected!" : "failed");

  Serial.printlnf("Temp sensor chip type: %x", ds18b20.getChipType());
  Serial.printlnf("Temp sensor chip name: %s", ds18b20.getChipName());

  pinMode(P_BTN_UP, INPUT_PULLDOWN);
  pinMode(P_BTN_DOWN, INPUT_PULLDOWN);
  pinMode(P_BTN_SET, INPUT_PULLDOWN);

  pinMode(P_CTRL_COOL, OUTPUT);
  pinMode(P_CTRL_HEAT, OUTPUT);
  
  Particle.function("setTargetTempF", cldSetTargetTemp);
  Particle.function("setMode", cldSetMode);

  Particle.variable("currentTemperature", fahrenheit);
  Particle.variable("targetTempPrecision", precision);
  Particle.variable("targetTemperature", targetTemp);
  Particle.variable("calibrationDifferential", calibrationDiff);

  lcd.setCursor(0,2);
  lcd.print("Data Svc... ");
  int pingStatus = ping();
  switch(pingStatus) {
    case 0:
      lcd.print("success!");
      break;
    case 1:
      lcd.print("failed");
      break;
    case 2:
      lcd.print("error");
      break;
  }

  Serial.printlnf("Device Id: %s", deviceId);
  Serial.printlnf("Target Temp: %.2f", targetTemp);
  Serial.printlnf("Target Temp Precision: %.2f", precision);
  Serial.printlnf("Calibration Differential: %.2f", calibrationDiff);

  tempTimer.start();
  refreshDisplayTimer.start();
  setModeTimer.start();
  pingServiceTimer.start();

  Serial.println("Initialization complete!");
  lcd.setCursor(0,0);
  lcd.print("Initializing... done!");

  setMode(MODE_DEFAULT);
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {
  int _btnUpState = digitalRead(P_BTN_UP);
  if (_btnUpState != btnUpState) {
    if(btnUpState == HIGH) {
      upBtnPressed();
    } else {
      btnUpLongPressedHandled = false;
      btnUpPressStart = millis();
    }
    btnUpState = _btnUpState;
  } else if (btnUpState == HIGH) {
    uint32_t now = millis();
    if ((now - btnUpPressStart) >= LONG_BTN_PRESS_MILS && !btnUpLongPressedHandled) {
      upBtnPressedLong();
    }
  }

  int _btnDownState = digitalRead(P_BTN_DOWN);
  if (_btnDownState != btnDownState) {
    if(btnDownState == HIGH) {
      downBtnPressed();
    } else {
      btnDownLongPressedHandled = false;
      btnDownPressStart = millis();
    }
    btnDownState = _btnDownState;
  } else if (btnDownState == HIGH) {
    uint32_t now = millis();
    if ((now - btnDownPressStart) >= LONG_BTN_PRESS_MILS && !btnDownLongPressedHandled) {
      downBtnPressedLong();
    }
  }

  int _btnSetState = digitalRead(P_BTN_SET);
  if (_btnSetState != btnSetState) {
    if(btnSetState == HIGH) {
      setBtnPressed();
    } else {
      btnSetLongPressedHandled = false;
      btnSetPressStart = millis();
    }
    btnSetState = _btnSetState;
  } else if (btnSetState == HIGH) {
    uint32_t now = millis();
    if ((now - btnSetPressStart) >= LONG_BTN_PRESS_MILS && !btnSetLongPressedHandled) {
      setBtnPressedLong();
    }
  }
}

void setState(int state) {
  int _prevDisplayState = displayState;
  
  if (_prevDisplayState == STATE_DEFAULT) {
    refreshDisplayTimer.stop();
  }

  displayState = state;
  switch(state) {
    case STATE_DEFAULT:
      refreshDisplayTimer.start();
      break;
    case STATE_MENU:
      if (_prevDisplayState == STATE_DEFAULT) {
        menuFirstItemIndex = 0;
        menuItemIndex = 0;
      }
      break;
    case STATE_MENU_SET_TEMP:
      tTargetTemp = targetTemp;
      break;
    case STATE_MENU_CALIBRATE:
      calibrationTemp = (fahrenheit == NAN || fahrenheit <= 0) ? targetTemp : fahrenheit;
      break;
    case STATE_MENU_TEMP_VAR:
      tTempVariable = tempVariable;
      break;
    case STATE_MENU_SET_PRECISION:
      tPrecision = precision;
      break;
  }
  refreshDisplay(true);
}

void upBtnPressed() {
  uint32_t now = millis();
  if ((now - btnUpPressStart) >= LONG_BTN_PRESS_MILS) {
    return;
  }
  Serial.println("Up btn pressed");
  switch(displayState) {
    case STATE_MENU_SET_TEMP:
      tTargetTemp = tTargetTemp + .5;
      refreshDisplay();
      break;
    case STATE_MENU:
      if (menuItemIndex == 0) {
        menuItemIndex = (MENU_ITEM_SIZE - 1);
        menuFirstItemIndex = menuItemIndex - 2;
      } else {
        menuItemIndex = menuItemIndex - 1;
        if(menuFirstItemIndex > menuItemIndex) {
          menuFirstItemIndex = menuItemIndex;
        }
      }
      refreshDisplay();
      break;
    case STATE_MENU_CALIBRATE:
      calibrationTemp = calibrationTemp + .1;
      refreshDisplay();
      break;
  }
}

void upBtnPressedLong() {
  btnUpLongPressedHandled = true;
}

void downBtnPressed() {
  uint32_t now = millis();
  if ((now - btnDownPressStart) >= LONG_BTN_PRESS_MILS) {
    Serial.println("button pressed function ignored since the button is long pressed");
    return;
  }
  Serial.println("Down btn pressed");
  switch(displayState) {
    case STATE_MENU_SET_TEMP:
      tTargetTemp = tTargetTemp - .5;
      refreshDisplay();
      break;
    case STATE_MENU:
      if (menuItemIndex == (MENU_ITEM_SIZE - 1)) {  
        menuItemIndex = 0;
        menuFirstItemIndex = 0;
      } else {
        menuItemIndex = menuItemIndex + 1;
        if ((menuItemIndex - menuFirstItemIndex) > 2) {
          menuFirstItemIndex = menuItemIndex - 2;
        }
      }
      refreshDisplay();
      break;
    case STATE_MENU_CALIBRATE:
      calibrationTemp = calibrationTemp - .1;
      refreshDisplay();
      break;
  }
}

void downBtnPressedLong() {
  btnDownLongPressedHandled = true;
}

void setBtnPressed() {
  uint32_t now = millis();
  if ((now - btnSetPressStart) >= LONG_BTN_PRESS_MILS) {
    return;
  }

  switch(displayState) {
    case STATE_MENU:
      switch(menuItemIndex) {
        case 0:
          setState(STATE_MENU_SET_TEMP);
          break;
        case 1:
          setMode(currentMode == MODE_OFF ? MODE_HOLD : MODE_OFF);
          setState(STATE_DEFAULT);
          break;
        case 2:
          calibrationTemp = fahrenheit;
          setState(STATE_MENU_CALIBRATE);
          break;
        case 3:
          setState(STATE_DEFAULT);
          break;
      }
      break;
    case STATE_MENU_SET_TEMP:
      targetTemp = tTargetTemp;
      // TODO: Update data service
      // deviceService.updateTargetTemp(deviceId, targetTemp);
      setState(STATE_MENU);
      break;
    case STATE_MENU_CALIBRATE:
      calibrationDiff = calibrationTemp - fahrenheit;
      // TODO: Update data service
      // deviceService.updateCalibrationDiff(deviceId, calibrationDiff);
      setState(STATE_MENU);
      break;
    case STATE_MENU_TEMP_VAR:
      tempVariable = tTempVariable;
      // TODO: Update data service
      // deviceService.updateTempVariable(deviceId, tempVariable);
      setState(STATE_MENU);
      break;
    case STATE_MENU_SET_PRECISION:
      precision = tPrecision;
      // TODO: Update data service
      // deviceService.updatePrecision(deviceId, precision);
      setState(STATE_MENU);
      break;
  }
}

void setBtnPressedLong() {
  btnSetLongPressedHandled = true;

  switch(displayState) {
    case STATE_DEFAULT:
      setState(STATE_MENU);
      break;
    case STATE_MENU:
      setState(STATE_DEFAULT);
      break;
    case STATE_MENU_CALIBRATE:
    case STATE_MENU_SET_PRECISION:
    case STATE_MENU_SET_TEMP:
    case STATE_MENU_TEMP_VAR:
      setState(STATE_MENU);
      break;
  }
  
}

void setMode(int mode) {
  //int _previousMode = currentMode;
  currentMode = mode;
}

void setModeCtl() {
  if(currentMode != MODE_OFF) {
    if(fahrenheit > 0 && fahrenheit != NAN) {
      setMode(fahrenheit > (targetTemp + tempVariable) ?  MODE_COOLING : fahrenheit < (targetTemp - tempVariable) ? MODE_HEATING : MODE_HOLD);
    } else {
      setMode(MODE_HOLD);
      Serial.printlnf("Temp sample is %f, setting to Hold as something is weird or the systems just started up and no temp has been taken", fahrenheit);
    }
  }

  switch(currentMode) {
    case MODE_COOLING:
      digitalWrite(P_CTRL_COOL, HIGH);
      digitalWrite(P_CTRL_HEAT, LOW);
      break;
    case MODE_HEATING:
      digitalWrite(P_CTRL_COOL, LOW);
      digitalWrite(P_CTRL_HEAT, MODE_HEATING);
      break;
    case MODE_HOLD:
      digitalWrite(P_CTRL_COOL, LOW);
      digitalWrite(P_CTRL_HEAT, LOW);
      break;
    case MODE_OFF:
      digitalWrite(P_CTRL_COOL, LOW);
      digitalWrite(P_CTRL_HEAT, LOW);
      break;
  }
}

void refreshDisplayWrapper() {
  refreshDisplay(false);
}

void refreshDisplay(bool clear){
  if (refreshLock) {
    return;
  }
  refreshLock = true;

  if (clear) {
    lcd.clear();
  }

  if (displayState == STATE_DEFAULT) {
    lcd.setCursor(0,0);
    String statusStr = "UNKNOWN";
    switch(currentMode) {
      case MODE_COOLING  :
        statusStr = "Cooling";
        break;
      case MODE_HEATING:
        statusStr = "Heating";
        break;
      case MODE_HOLD:
        statusStr = "Hold   ";
        break;
      case MODE_OFF:
        statusStr = "Off    ";
        break;
    }
    lcd.printf("Status: %s     ",  statusStr.c_str());

    lcd.setCursor(0,1);
    lcd.printf("Current Temp: %.1f%cf", fahrenheit, CHAR_DEGREE);

    lcd.setCursor(0,2);
    if (currentMode != MODE_OFF) {
      lcd.printf("Target Temp:  %.1f%cf", targetTemp, CHAR_DEGREE);
    } else {
      lcd.printf("                    "); 
    }
    lcd.setCursor(0,3);
    lcd.printf("                    ");

  }
  if (displayState == STATE_MENU) {
    lcd.setCursor(0,0);
    lcd.printf("Menu:               ");
    for (int i = 0; i <= 2; i++) {
      lcd.setCursor(0,i+1);
      String m = MENU_ITEMS[menuFirstItemIndex + i];
      if (m == MENU_SET_MODE_OPTION) {
        m = "Turn ";
        m.concat(currentMode == MODE_OFF ? "On " : "Off");
        m.concat("          ");
      }
      lcd.printf("%c %s", menuItemIndex == (i + menuFirstItemIndex) ? CHAR_ARROW : CHAR_SPACE, m.c_str());
    }
  }
  if (displayState == STATE_MENU_SET_TEMP) {
    lcd.setCursor(0,0);
    lcd.printf("Set Target Temp:    ");
    lcd.setCursor(0,1);
    lcd.printf("                    ");
    lcd.setCursor(0,2);
    lcd.printf("Target Temp:  %3.1f%cf", tTargetTemp, CHAR_DEGREE);
    lcd.setCursor(0,3);
    lcd.printf("                    ");
  }
  if (displayState == STATE_MENU_CALIBRATE) {
    lcd.setCursor(0,0);
    lcd.printf("Calibrate:          ");
    lcd.setCursor(0,1);
    lcd.printf("                    ");
    lcd.setCursor(0,2);
    lcd.printf("Control Temp: %3.1f%cf", calibrationTemp, CHAR_DEGREE);
    lcd.setCursor(0,3);
    lcd.printf("                    ");
  }
  refreshLock = false;
}

void getTemp(){
  float _temp;
  int   i = 0;

  do {
    _temp = ds18b20.getTemperature();
  } while (!ds18b20.crcCheck() && MAXRETRY > i++);

  if (i < MAXRETRY) {
    double _prev = fahrenheit;
    fahrenheit = ds18b20.convertToFahrenheit(_temp) + calibrationDiff;
    
    if(_prev != NAN && _prev > 0 && _prev < 100 && fahrenheit != NAN && fahrenheit > 0 && fahrenheit < 100) {
      char _p[8];
      snprintf(_p, 8, "%.1f", _prev);
      char _t[8];
      snprintf(_t, 8, "%.1f", fahrenheit);
      if(strcmp(_p, _t) != 0) {
        Serial.printlnf("Temp has changed (after rounding).  Old: %s, New: %s.  Updating database.", _p, _t);
        // TODO update service
        // deviceService.sendStats(deviceId, {fahrenheit});
      }
    }

    Serial.println(fahrenheit);
  }
  else {
    fahrenheit = NAN;
    Serial.println("Invalid reading");
  }
  msLastSample = millis();
}

int cldSetTargetTemp(String data) {
  float t = data.toFloat();
  if (t > 0) {
    targetTemp = t;
  }

  return 0;
}

int cldSetMode(String data) {
  data = data.toLowerCase();

  if (data == "off") {
    setMode(MODE_OFF);
  }

  return 0;
}

int ping() {
  bool success = dataService.ping();
  if(success){
      Serial.println("Service is available!!");

      if(deviceId.length() == 0) {
        Serial.println("Device Id from the data service has not been set, retrieving.");

        device_data_t deviceData = dataService.findDevice(manufacturerId);

        if(deviceData.isNull) {
            Serial.println("Device not found on server.  Attempting to register");

            deviceData = dataService.registerDevice(manufacturerId, targetTemp, calibrationDiff);
        }

        if(!deviceData.isNull) {
            deviceId = deviceData.id;
            targetTemp = deviceData.targetTemp;
            calibrationDiff = deviceData.calibrationDiff;
        } else {
          return 2;
        }
      }
  } else {
      Serial.println("Service is currently unavailable.");
      return 1;
  }

  return 0;
}