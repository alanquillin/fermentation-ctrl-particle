/*
 * Project fermentation-controller
 * Description:
 * Author: Alan Quillin
 */

#include <LiquidCrystal_I2C_Spark.h>
#include <DS18B20.h>
#include <math.h>
#include <service.h>
#include <CircularBuffer.h>

const LogLevel APP_LOG_LEVEL = LOG_LEVEL_ALL;
SerialLogHandler logHandler(LOG_LEVEL_WARN, { // Logging level for non-application messages
    { "app", APP_LOG_LEVEL }, // Logging level for application messages
    { "app.service", LOG_LEVEL_ALL }
});

//func prototypes
void refreshDisplay(bool clear=false);

//pin definitions
const int16_t P_DS_DATA   = D2;
//const int16_t P_DS_DATA   = D6;
const int16_t P_BTN_UP    = A0;
const int16_t P_BTN_DOWN  = A1;
const int16_t P_BTN_SET   = A2;
//const int16_t P_CTRL_COOL = D4;
//const int16_t P_CTRL_HEAT = D5;
const int16_t P_CTRL_COOL = D3;
const int16_t P_CTRL_HEAT = D4;
const int16_t EXTRA_GND[] = {D5};
const int16_t EXTRA_PWR[] = {A3};

//charactors
const uint8_t CHAR_SPACE  = 0x20;
const uint8_t CHAR_ARROW  = 0x7E;
const uint8_t CHAR_DEGREE = 0xDF;

// modes
const uint8_t MODE_COOLING = 1;
const uint8_t MODE_HEATING = 2;
const uint8_t MODE_HOLD    = 3;
const uint8_t MODE_OFF     = 0;

// states
const uint8_t STATE_DEFAULT            = 0;
const uint8_t STATE_MENU               = 1;
const uint8_t STATE_MENU_SET_TEMP      = 2;
const uint8_t STATE_MENU_CALIBRATE     = 3;
const uint8_t STATE_MENU_SET_PRECISION = 4;
const uint8_t STATE_MENU_HEAT_DIFF     = 5;
const uint8_t STATE_MENU_COOL_DIFF     = 6;
const uint8_t STATE_MENU_RESET_CONFIG  = 7;

// other constants
const uint8_t MAXRETRY = 4;
const uint16_t LONG_BTN_PRESS_MILS = 2000;
const uint16_t STATS_PUSH_FREQUENCY_MS = 5000;

// dataservice
const String HOST = "<__IP_Address__>";
const uint16_t PORT = 80;

CircularBuffer<device_stats_t,100> stats;

LiquidCrystal_I2C lcd(0x27, 20, 4);
DS18B20 ds18b20(P_DS_DATA, true);

DataService dataService(HOST, PORT);
bool serviceReady = false;
uint32_t serviceFailCnt = 0;
uint32_t lastServiceCheck;
uint32_t lastStatsPush;
String manufacturerId;
String deviceId;
byte mac[6];

struct Config {
  double targetTemp;
  double coolingDifferential;
  double heatingDifferential;
  double tempPrecision;
  double calibrationDiff;
  bool programOn;
};
Config config = {68, 1, 1, .5, 0, true};

double fahrenheit = 0;
double prevFahrenheit = fahrenheit;
double tTargetTemp = config.targetTemp;
double tCoolingDifferential = config.coolingDifferential;
double tHeatingDifferential = config.heatingDifferential;
double tTempPrecision = config.tempPrecision;
double calibrationTemp;
int currentMode = MODE_HOLD;
int displayState = STATE_DEFAULT;
bool refreshLock = false;
uint32_t msLastSample;
long latestMenuActivityTS;

// menu state vars
const String MENU_SET_MODE_OPTION = "<SET MODE>";
const uint8_t MENU_ITEM_SIZE = 8;
const String MENU_ITEMS[MENU_ITEM_SIZE] = {
  "Set Target Temp   ",
  MENU_SET_MODE_OPTION,
  "Calibrate Temp    ",
  "Set Heating Diff  ",
  "Set Cooling Diff  ",
  "Set Temp Precision",
  "Reset Config      ",
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
Timer menuInactivityTimer(2000, checkMenuInactivity);

// setup() runs once, when the device is first turned on.
void setup() {
  // Put initialization like pinMode and begin functions here.
  Serial.begin(9600);
  
  Log.trace("Initializing Pins.");
  pinMode(P_BTN_UP, INPUT_PULLDOWN);
  pinMode(P_BTN_DOWN, INPUT_PULLDOWN);
  pinMode(P_BTN_SET, INPUT_PULLDOWN);

  pinMode(P_CTRL_COOL, OUTPUT);
  pinMode(P_CTRL_HEAT, OUTPUT);

  for(uint i=0; i < arraySize(EXTRA_GND); i++) {
    pinMode(EXTRA_GND[i], OUTPUT);
    digitalWrite(EXTRA_GND[i], LOW);
  }

  for(uint i=0; i < arraySize(EXTRA_PWR); i++) {
    pinMode(EXTRA_PWR[i], OUTPUT);
    digitalWrite(EXTRA_PWR[i], HIGH);
  }
  Log.trace("Pin initialization done.");

  Log.info("Starting up...");
  lcd.init();
  lcd.backlight();
  lcd.clear();
  Log.info("Initializing...");
  lcd.print("Initializing...");
  
  manufacturerId = System.deviceID();

  Log.trace("Device Manufacturer Id: %s", manufacturerId.c_str());
  WiFi.macAddress(mac);
  Log.trace("Wifi MAC address: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Log.trace("Wifi IP address: %s", WiFi.localIP().toString().c_str());

  lcd.setCursor(0,1);
  lcd.printf("Wifi... %s", WiFi.ready() ? "connected!" : "failed");

  Log.trace("Temp sensor chip type: %x", ds18b20.getChipType());
  Log.trace("Temp sensor chip name: %s", ds18b20.getChipName());
  
  Log.trace("Setting up cloud functions and variables.");
  Particle.function("setTargetTempF", cldSetTargetTemp);
  Particle.function("setMode", cldSetMode);

  Particle.variable("currentTemperature", fahrenheit);
  Particle.variable("tempPrecision", config.tempPrecision);
  Particle.variable("targetTemperature", config.targetTemp);
  Particle.variable("calibrationDifferential", config.calibrationDiff);
  Log.trace("Cloud stuff all set up!");
  
  loadConfig();

  lcd.setCursor(0,2);
  lcd.print("Data Svc... ");
  uint8_t pingStatus = ping();
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

  Log.trace("Device Id: %s", deviceId.c_str());
  Log.trace("Target Temp: %.2f", config.targetTemp);
  Log.trace("Target Temp Precision: %.2f", config.tempPrecision);
  Log.trace("Calibration Differential: %.2f", config.calibrationDiff);
  Log.trace("Heating Differential: %.2f", config.heatingDifferential);
  Log.trace("Cooling Differential: %.2f", config.coolingDifferential);
  Log.trace("Program on: %s", config.programOn ? "true" : "false");

  tempTimer.start();
  refreshDisplayTimer.start();
  setModeTimer.start();

  Log.info("Initialization complete!");
  lcd.setCursor(0,0);
  lcd.print("Initializing... done!");

  setMode(config.programOn ? MODE_HOLD : MODE_OFF);
  setState(STATE_DEFAULT);
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {
  bool isBtnPresseed = false;
  int _btnUpState = digitalRead(P_BTN_UP);
  uint32_t now = millis();
  if (_btnUpState != btnUpState) {
    if(btnUpState == HIGH) {
      upBtnPressed();
    } else {
      isBtnPresseed = true;
      btnUpLongPressedHandled = false;
      btnUpPressStart = millis();
    }
    btnUpState = _btnUpState;
  } else if (btnUpState == HIGH) {
    isBtnPresseed = true;
    if ((now - btnUpPressStart) >= LONG_BTN_PRESS_MILS && !btnUpLongPressedHandled) {
      upBtnPressedLong();
    }
  }

  int _btnDownState = digitalRead(P_BTN_DOWN);
  if (_btnDownState != btnDownState) {
    if(btnDownState == HIGH) {
      downBtnPressed();
    } else {
      isBtnPresseed = true;
      btnDownLongPressedHandled = false;
      btnDownPressStart = millis();
    }
    btnDownState = _btnDownState;
  } else if (btnDownState == HIGH) {
    isBtnPresseed = true;
    
    if ((now - btnDownPressStart) >= LONG_BTN_PRESS_MILS && !btnDownLongPressedHandled) {
      downBtnPressedLong();
    }
  }

  int _btnSetState = digitalRead(P_BTN_SET);
  if (_btnSetState != btnSetState) {
    if(btnSetState == HIGH) {
      setBtnPressed();
    } else {
      isBtnPresseed = true;
      btnSetLongPressedHandled = false;
      btnSetPressStart = millis();
    }
    btnSetState = _btnSetState;
  } else if (btnSetState == HIGH) {
    isBtnPresseed = true;
    if ((now - btnSetPressStart) >= LONG_BTN_PRESS_MILS && !btnSetLongPressedHandled) {
      setBtnPressedLong();
    }
  }

   if (prevFahrenheit != fahrenheit) {
    if(prevFahrenheit != NAN && prevFahrenheit > 0 && prevFahrenheit < 100 && fahrenheit != NAN && fahrenheit > 0 && fahrenheit < 100) {
      float diff = prevFahrenheit - fahrenheit;
      Log.trace("getTemp: _prev: %f, diff: %f", prevFahrenheit, diff);
      if (diff <= -0.02 || diff >= 0.02) {
        Log.trace("getTemp: Temp has changed (after rounding).  Old: %f, New: %f, diff: %f.  Updating database.", prevFahrenheit, fahrenheit, diff);
        prevFahrenheit = fahrenheit;
        stats.push({fahrenheit, Time.now()}); // put stat record to the end of the queue
      }
    } 
  }

  if (!isBtnPresseed && displayState == STATE_DEFAULT) {
    // uint16_t nextCheckFreq = serviceReady ? 10000 : serviceFailCnt > 20 ? 20000 : (serviceFailCnt * 1000);
    // uint32_t elapsed = (now - lastServiceCheck);
    // if (elapsed >= nextCheckFreq) {
    //   Log.trace("%.1f seconds have elapsed since the last service check...", (float(elapsed) / 1000));
    //   Log.info("Pinging service");
    //   ping();
    // }
    // if (serviceReady && (lastStatsPush == 0 || (now - lastStatsPush > STATS_PUSH_FREQUENCY_MS))) {
    //   pushStats();
    // }
  }
}

void setState(int nextState) {
  int _prevDisplayState = displayState;
  
  if (_prevDisplayState == STATE_DEFAULT) {
    refreshDisplayTimer.stop();
  }

  displayState = nextState;
  switch(nextState) {
    case STATE_DEFAULT:
      menuInactivityTimer.stop();
      refreshDisplayTimer.start();
      break;
    case STATE_MENU:
      latestMenuActivityTS = Time.now();
      menuInactivityTimer.start();
      if (_prevDisplayState == STATE_DEFAULT) {
        menuFirstItemIndex = 0;
        menuItemIndex = 0;
      }
      break;
    case STATE_MENU_SET_TEMP:
      tTargetTemp = config.targetTemp;
      break;
    case STATE_MENU_CALIBRATE:
      calibrationTemp = (fahrenheit == NAN || fahrenheit <= 0) ? config.targetTemp : fahrenheit;
      break;
    case STATE_MENU_HEAT_DIFF:
      tHeatingDifferential = config.heatingDifferential;
      break;
    case STATE_MENU_COOL_DIFF:
      tCoolingDifferential = config.coolingDifferential;
      break;
    case STATE_MENU_SET_PRECISION:
      tTempPrecision = config.tempPrecision;
      break;
  }
  refreshDisplay(true);
}

void upBtnPressed() {
  uint32_t now = millis();
  if ((now - btnUpPressStart) >= LONG_BTN_PRESS_MILS) {
    Log.trace("up pressed function ignored since the button is long pressed");
    return;
  }
  Log.trace("Up btn pressed");
  
  if (displayState != STATE_DEFAULT) {
    latestMenuActivityTS = Time.now();
  }

  switch(displayState) {
    case STATE_MENU_SET_TEMP:
      tTargetTemp = tTargetTemp + config.tempPrecision;
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
    case STATE_MENU_SET_PRECISION:
      tTempPrecision = tTempPrecision + .1;
      refreshDisplay();
      break;
    case STATE_MENU_HEAT_DIFF:
      tHeatingDifferential = tHeatingDifferential + config.tempPrecision;
      refreshDisplay();
      break;
    case STATE_MENU_COOL_DIFF:
      tCoolingDifferential = tCoolingDifferential + config.tempPrecision;
      refreshDisplay();
      break;
    case STATE_MENU_RESET_CONFIG:
      config = {68, 1, 1, .5, 0, config.programOn};
      saveConfig();
      setState(STATE_MENU);
      break;
  }
}

void upBtnPressedLong() {
  btnUpLongPressedHandled = true;
}

void downBtnPressed() {
  uint32_t now = millis();
  if ((now - btnDownPressStart) >= LONG_BTN_PRESS_MILS) {
    Log.trace("button pressed function ignored since the button is long pressed");
    return;
  }
  Log.trace("Down btn pressed");
  
  if (displayState != STATE_DEFAULT) {
    latestMenuActivityTS = Time.now();
  }

  switch(displayState) {
    case STATE_MENU_SET_TEMP:
      tTargetTemp = tTargetTemp - config.tempPrecision;
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
    case STATE_MENU_SET_PRECISION:
      if (tTempPrecision > .1) {
        tTempPrecision = tTempPrecision - .1;
        refreshDisplay();
      }
      break;
    case STATE_MENU_HEAT_DIFF:
      if(tHeatingDifferential > 0) {
        tHeatingDifferential = tHeatingDifferential - config.tempPrecision;
        if (tHeatingDifferential < 0) {
          tHeatingDifferential = 0;
        }
        refreshDisplay();
      }
      break;
    case STATE_MENU_COOL_DIFF:
      if(tCoolingDifferential > 0) {
        tCoolingDifferential = tCoolingDifferential - config.tempPrecision;
        if (tCoolingDifferential < 0) {
          tCoolingDifferential = 0;
        }
        refreshDisplay();
      }
      break;
    case STATE_MENU_RESET_CONFIG:
      setState(STATE_MENU);
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

  if (displayState != STATE_DEFAULT) {
    latestMenuActivityTS = Time.now();
  }

  switch(displayState) {
    case STATE_MENU:
      switch(menuItemIndex) {
        case 0:
          setState(STATE_MENU_SET_TEMP);
          break;
        case 1:
          setMode(currentMode == MODE_OFF ? MODE_HOLD : MODE_OFF);
          config.programOn = currentMode != MODE_OFF;
          saveConfig();
          // TODO: Update data service
          // deviceService.updateProgramState(deviceId, config.programOn);
          setState(STATE_DEFAULT);
          break;
        case 2:
          calibrationTemp = fahrenheit;
          setState(STATE_MENU_CALIBRATE);
          break;
        case 3:
          setState(STATE_MENU_HEAT_DIFF);
          break;
        case 4:
          setState(STATE_MENU_COOL_DIFF);
          break;
        case 5:
          setState(STATE_MENU_SET_PRECISION);
          break;
        case 6:
          setState(STATE_MENU_RESET_CONFIG);
          break;
        case 7:
          setState(STATE_DEFAULT);
          break;
      }
      break;
    case STATE_MENU_SET_TEMP:
      config.targetTemp = tTargetTemp;
      saveConfig();
      // TODO: Update data service
      // deviceService.updateTargetTemp(deviceId, config.targetTemp);
      setState(STATE_MENU);
      break;
    case STATE_MENU_CALIBRATE:
      config.calibrationDiff = calibrationTemp - fahrenheit;
      saveConfig();
      // TODO: Update data service
      // deviceService.updateCalibrationDiff(deviceId, config.calibrationDiff);
      setState(STATE_MENU);
      break;
    case STATE_MENU_HEAT_DIFF:
      config.heatingDifferential = tHeatingDifferential;
      saveConfig();
      // TODO: Update data service
      // deviceService.updateHeatingDifferential(deviceId, config.heatingDifferential);
      setState(STATE_MENU);
      break;
    case STATE_MENU_COOL_DIFF:
      config.coolingDifferential = tCoolingDifferential;
      saveConfig();
      // TODO: Update data service
      // deviceService.updateCoolingDifferential(deviceId, config.coolingDifferential);
      setState(STATE_MENU);
      break;
    case STATE_MENU_SET_PRECISION:
      config.tempPrecision = tTempPrecision;
      saveConfig();
      // TODO: Update data service
      // deviceService.updatePrecision(deviceId, config.tempPrecision);
      setState(STATE_MENU);
      break;
  }
}

void setBtnPressedLong() {
  btnSetLongPressedHandled = true;

  if (displayState != STATE_DEFAULT) {
    latestMenuActivityTS = Time.now();
  }

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
    case STATE_MENU_HEAT_DIFF:
    case STATE_MENU_COOL_DIFF:
      setState(STATE_MENU);
      break;
  }
  
}

void setMode(uint8_t mode) {
  //int _previousMode = currentMode;
  currentMode = mode;
}

void setModeCtl() {
  //int _previousMode = currentMode;

  if(currentMode != MODE_OFF) {
    if(fahrenheit > 0 && fahrenheit != NAN) {
      switch(currentMode) {
        case MODE_COOLING:
          if(fahrenheit <= config.targetTemp) {
            setMode(MODE_HOLD);
          }
          break;
        case MODE_HEATING:
          if(fahrenheit >= config.targetTemp) {
            setMode(MODE_HOLD);
          }
          break;
        case MODE_HOLD:
          setMode(fahrenheit > (config.targetTemp + config.coolingDifferential) ?  MODE_COOLING : fahrenheit < (config.targetTemp - config.heatingDifferential) ? MODE_HEATING : MODE_HOLD);
          break;
      }
    } else {
      setMode(MODE_HOLD);
      //Log.trace("Temp sample is %f, setting to Hold as something is weird...", fahrenheit);
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
      lcd.printf("Target Temp:  %.1f%cf", config.targetTemp, CHAR_DEGREE);
    } else {
      lcd.printf("                    "); 
    }
    lcd.setCursor(0,3);
    lcd.printf("                    ");

  }
  if (displayState == STATE_MENU) {
    lcd.setCursor(0,0);
    lcd.printf("Menu:               ");
    for (uint8_t i = 0; i <= 2; i++) {
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
  if (displayState == STATE_MENU_HEAT_DIFF) {
    lcd.setCursor(0,0);
    lcd.printf("Set Heating Diff");
    lcd.setCursor(0,1);
    lcd.printf("                    ");
    lcd.setCursor(0,2);
    lcd.printf("Differential: %3.1f%cf", tHeatingDifferential, CHAR_DEGREE);
    lcd.setCursor(0,3);
    lcd.printf("                    ");
  }
  if (displayState == STATE_MENU_COOL_DIFF) {
    lcd.setCursor(0,0);
    lcd.printf("Set Cooling Diff");
    lcd.setCursor(0,1);
    lcd.printf("                    ");
    lcd.setCursor(0,2);
    lcd.printf("Differential: %3.1f%cf", tCoolingDifferential, CHAR_DEGREE);
    lcd.setCursor(0,3);
    lcd.printf("                    ");
  }
  if (displayState == STATE_MENU_SET_PRECISION) {
    lcd.setCursor(0,0);
    lcd.printf("Set Temp Precision");
    lcd.setCursor(0,1);
    lcd.printf("                    ");
    lcd.setCursor(0,2);
    lcd.printf("Value: %3.1f%cf", tTempPrecision, CHAR_DEGREE);
    lcd.setCursor(0,3);
    lcd.printf("                    ");
  }
  if (displayState == STATE_MENU_RESET_CONFIG) {
    lcd.setCursor(0,0);
    lcd.printf("Set Temp Precision");
    lcd.setCursor(0,1);
    lcd.printf("                    ");
    lcd.setCursor(0,2);
    lcd.printf("Up:   Confirm       ");
    lcd.setCursor(0,3);
    lcd.printf("Down: Cancel        ");
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
    prevFahrenheit = fahrenheit;
    fahrenheit = ds18b20.convertToFahrenheit(_temp) + config.calibrationDiff;

    if (APP_LOG_LEVEL == LOG_LEVEL_ALL || APP_LOG_LEVEL == LOG_LEVEL_TRACE) {
      Serial.printlnf("Temperature reading: %.2f", fahrenheit);
    }

  }
  else {
    fahrenheit = NAN;
    Serial.println("[ERROR] getTemp: Invalid temperature reading...");
  }
  msLastSample = millis();
}

int cldSetTargetTemp(String data) {
  float t = data.toFloat();
  if (t > 0) {
    config.targetTemp = t;
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

uint8_t ping() {
  lastServiceCheck = millis();
  serviceReady = dataService.ping();

  if(serviceReady){
    serviceFailCnt = 0;
    Log.info("Service is available!!");

    if(deviceId.length() == 0) {
      Log.info("Device Id from the data service has not been set, retrieving.");

      device_data_t deviceData = dataService.findDevice(manufacturerId);

      if(deviceData.isNull) {
          Log.warn("Device not found on server.  Attempting to register");

          deviceData = dataService.registerDevice(manufacturerId, config.targetTemp, config.calibrationDiff);
      }

      if(!deviceData.isNull) {
          deviceId = deviceData.id;
          config.targetTemp = deviceData.targetTemp;
          config.calibrationDiff = deviceData.calibrationDiff;
          config.coolingDifferential = deviceData.coolingDifferential;
          config.heatingDifferential = deviceData.heatingDifferential;
          config.tempPrecision = deviceData.tempPrecision;
          config.programOn = deviceData.programOn;

          saveConfig();
      } else {
        Log.error("Registration failed, no device information returned.");
        return 2;
      }
    }
  } else {
    serviceFailCnt = serviceFailCnt + 1;
    Log.error("Service is currently unavailable.");
    return 1;
  }

  return 0;
}

uint8_t pushStats() {
  lastStatsPush = millis();
  if (stats.isEmpty()){
    Log.warn("Stat set is empty, nothing to push... there may be an issue with  the temperature probe.");
  }

  Log.trace("Sending temp stats to data service.");
  const uint8_t batchSize = 10;
  device_stats_t batch[batchSize];
  uint8_t i = 0;
  while(!stats.isEmpty() && i < batchSize) {
    device_stats_t s = stats.shift(); // retrieve and remove first stats item from the queue
    batch[i] = s;
    i = i + 1;
  }
  if(i > 0) {
    bool success = dataService.sendStats(deviceId, batch, i);
    if(success) {
      Log.trace("Successfully send %d stats records to the data service", i);
    } else {
      Log.trace("Failed to send %d stats records to data service.", i);
    }

    return success ? 0 : 1;
  } else {
    Log.error("No stats records were batched to send to the data service... this should not happen as the buffer states it is not empty... WTH!");
  }

  return 0;
}

void saveConfig() {
  EEPROM.put(0, config);

  Log.trace("Saved Config: Target Temp: %.2f", config.targetTemp);
  Log.trace("Saved Config: Target Temp Precision: %.2f", config.tempPrecision);
  Log.trace("Saved Config: Calibration Differential: %.2f", config.calibrationDiff);
  Log.trace("Saved Config: Heating Differential: %.2f", config.heatingDifferential);
  Log.trace("Saved Config: Cooling Differential: %.2f", config.coolingDifferential);
  Log.trace("Saved Config: Program on: %s", config.programOn ? "true" : "false");
}

void loadConfig() {
  Config _config;
  EEPROM.get(0, _config);

  if (_config.targetTemp > 0) {
    Log.info("Loading config.");
    config.calibrationDiff = _config.calibrationDiff;
    config.coolingDifferential = _config.coolingDifferential;
    config.heatingDifferential = _config.heatingDifferential;
    config.targetTemp = _config.targetTemp;
    config.tempPrecision = _config.tempPrecision;
    config.programOn = _config.programOn;

    Log.trace("Loaded Config: Target Temp: %.2f", _config.targetTemp);
    Log.trace("Loaded Config: Target Temp Precision: %.2f", _config.tempPrecision);
    Log.trace("Loaded Config: Calibration Differential: %.2f", _config.calibrationDiff);
    Log.trace("Loaded Config: Heating Differential: %.2f", _config.heatingDifferential);
    Log.trace("Loaded Config: Cooling Differential: %.2f", _config.coolingDifferential);
    Log.trace("Loaded Config: Program on: %s", _config.programOn ? "true" : "false");
  } else {
    Log.info("No existing state found, ignoring.");
  }
}

void checkMenuInactivity() {
  if (displayState == STATE_DEFAULT) {
    return;
  }

  long inactivity = Time.now() - latestMenuActivityTS;
  if (inactivity > 30) {
    Serial.printlnf("[app.checkMenuInactivity] INFO: Menu inactivity has exceeded 30 seconds, returning to default screen.");
    menuInactivityTimer.stop();
    setState(STATE_DEFAULT);
  }
}