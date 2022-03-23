#include "diagnostics.h"
#include "constants.h"
#include "service.h"
#include <LiquidCrystal_I2C_Spark.h>
#include <DS18B20.h>

/**
* Constructor.
*/
Diagnostics::Diagnostics(LiquidCrystal_I2C& _lcd, DS18B20& _ds18b20, DataService& _dataService, void (*_onComplete)(), void (*_onCancel)())
    : lcd{_lcd}, ds18b20{_ds18b20}, dataService{_dataService}, logger("app.diagnostics")
{
    onComplete = _onComplete;
    onCancel = _onCancel;

    lcd.init();
    lcd.backlight();
    lcd.clear();
}

void Diagnostics::run() {
    logger.trace("Starting diagnostics.");

    errCnt = 0;
    wifiTestStatus = "pending...  ";
    wifiTestComplete = false;
    serviceTestStatus = "pending...  ";
    serviceTestComplete = false;
    tempProbeDriver = "";
    tempProbeTemp = 0;
    tempProbeTestComplete = false;

    digitalWrite(P_CTRL_COOL, LOW);
    digitalWrite(P_CTRL_HEAT, LOW);
    
    clearScreen();
    setState(INIT);
}

void Diagnostics::setState(States _state) {
    state = _state;

    refreshScreen();

    switch(state) {
        case WIFI:
            testWifi();
            break;
        case SERVICE:
            testService();
            break;
        case TEMP_PROBE:
            testTempProbe();
            break;
        case HEAT_OUTLET:
            testHeatOutlet();
            break;
        case COOL_OUTLET:
            testCoolOutlet();
            break;
    }
}

void Diagnostics::upBtnPressed(){
    switch(state) {
        case INIT:
            setState(WIFI);
            break;
        case WIFI:
            if(wifiTestComplete) {
                setState(SERVICE);
            }
            break;
        case SERVICE:
            if(serviceTestComplete) {
                setState(TEMP_PROBE);
            }
            break;
        case TEMP_PROBE:
            if(tempProbeTestComplete) {
                setState(UP_BTN);
            }
            break;
        case UP_BTN:
            lcd.setCursor(0,3);
            lcd.print("Passed!             ");
            delay(1000);
            setState(DOWN_BTN);
            break;
        case HEAT_OUTLET:
            digitalWrite(P_CTRL_HEAT, LOW);
            setState(COOL_OUTLET);
            break;
        case COOL_OUTLET:
            digitalWrite(P_CTRL_COOL, LOW);
            setState(RESULTS);
            break;
        case RESULTS:
            onComplete();
            break;
    }
}

void Diagnostics::downBtnPressed() {
    switch(state) {
        case INIT:
            onCancel();
            break;
        case WIFI:
            if(wifiTestComplete) {
                onCancel();
            }
            break;
        case SERVICE:
            if(serviceTestComplete) {
                onCancel();
            }
            break;
        case TEMP_PROBE:
            if(tempProbeTestComplete) {
                onCancel();
            }
            break;
        case DOWN_BTN:
            lcd.setCursor(0,3);
            lcd.print("Passed!             ");
            delay(1000);
            setState(SET_BTN);
            break;
        case HEAT_OUTLET:
            digitalWrite(P_CTRL_HEAT, LOW);
            errCnt = errCnt + 1;
            setState(COOL_OUTLET);
        case COOL_OUTLET:
            digitalWrite(P_CTRL_COOL, LOW);
            errCnt = errCnt + 1;
            setState(RESULTS);
    }
}

void Diagnostics::setBtnPressed() {
    switch(state) {
        case SET_BTN:
            lcd.setCursor(0,3);
            lcd.print("Passed!             ");
            delay(1000);
            setState(HEAT_OUTLET);
            break;
    }
}

void Diagnostics::clearScreen(){
    for(uint i=0; i < 4; i++) {
        lcd.setCursor(0, i);
        lcd.print(EMPTY_ROW);
    };
}

void Diagnostics::clearLines(uint8_t *lines, uint8_t size){
    for(uint i=0; i < size; i++) {
        lcd.setCursor(0, lines[i]);
        lcd.print(EMPTY_ROW);
    };
}

void Diagnostics::refreshScreen() {
    switch(state) {
        case INIT:
            lcd.setCursor(0,0);
            lcd.print("Start Diagnostics   ");
            lcd.setCursor(0,1);
            lcd.print(EMPTY_ROW);
            showConfirmMessage();
            break;
        case WIFI:
            lcd.setCursor(0,0);
            lcd.print("Testing Wifi:       ");
            lcd.setCursor(0,1);
            lcd.printf("status: %s", wifiTestStatus.c_str());
            if (wifiTestComplete) {
                showConfirmMessage();
            } else {
                lcd.setCursor(0, 2);
                lcd.print(EMPTY_ROW);
                lcd.setCursor(0, 3);
                lcd.print(EMPTY_ROW);
            }
            break;
        case SERVICE:
            lcd.setCursor(0,0);
            lcd.print("Testing Service API:");
            lcd.setCursor(0,1);
            lcd.printf("status: %s", serviceTestStatus.c_str());
            if (serviceTestComplete) {
                showConfirmMessage();
            } else {
                lcd.setCursor(0, 2);
                lcd.print(EMPTY_ROW);
                lcd.setCursor(0, 3);
                lcd.print(EMPTY_ROW);
            }
            break;
        case TEMP_PROBE:
            lcd.setCursor(0,0);
            lcd.print("Test Temp Probe:    ");
            
            if (tempProbeTestComplete) {
                lcd.setCursor(0, 1);
                lcd.printf("D: %s, T: %3.2f ", tempProbeDriver.c_str(), tempProbeTemp);
                showConfirmMessage();
            } else {
                lcd.setCursor(0, 1);
                lcd.print("Checking probe data.");
                lcd.setCursor(0, 2);
                lcd.print(EMPTY_ROW);
                lcd.setCursor(0, 3);
                lcd.print(EMPTY_ROW);
            }
            break;
        case UP_BTN:
            lcd.setCursor(0,0);
            lcd.print("Test Up Button:     ");
            lcd.setCursor(0, 1);
            lcd.print(EMPTY_ROW);
            lcd.setCursor(0,2);
            lcd.print("Press Up Button...  ");
            lcd.setCursor(0, 3);
            lcd.print(EMPTY_ROW);
            break;
        case DOWN_BTN:
            lcd.setCursor(0,0);
            lcd.print("Test Down Button:   ");
            lcd.setCursor(0, 1);
            lcd.print(EMPTY_ROW);
            lcd.setCursor(0,2);
            lcd.print("Press Down Button...");
            lcd.setCursor(0, 3);
            lcd.print(EMPTY_ROW);
            break;
        case SET_BTN:
            lcd.setCursor(0,0);
            lcd.print("Test Set Button:    ");
            lcd.setCursor(0, 1);
            lcd.print(EMPTY_ROW);
            lcd.setCursor(0,2);
            lcd.print("Press Set Button... ");
            lcd.setCursor(0, 3);
            lcd.print(EMPTY_ROW);
            break;
        case HEAT_OUTLET:
            lcd.setCursor(0,0);
            lcd.print("Test Heat Outlet:   ");
            lcd.setCursor(0,1);
            lcd.print("Outlet and LED on?");
            lcd.setCursor(0,2);
            lcd.print("Up:    Yes          ");
            lcd.setCursor(0,3);
            lcd.print("Down:  No           ");
            break;
        case COOL_OUTLET:
            lcd.setCursor(0,0);
            lcd.print("Test Cool Outlet:   ");
            lcd.setCursor(0,1);
            lcd.print("Outlet and LED on?");
            lcd.setCursor(0,2);
            lcd.print("Up:    Yes          ");
            lcd.setCursor(0,3);
            lcd.print("Down:  No           ");
            break;
        case RESULTS:
            lcd.setCursor(0,0);
            lcd.print("Results:            ");
            if (errCnt == 0) {
                lcd.setCursor(0, 1);
                lcd.print("Passed!             ");
                lcd.setCursor(0, 2);
                lcd.print(EMPTY_ROW);
            } else {
                lcd.setCursor(0, 1);
                lcd.print("Completed w/ errors ");
                lcd.setCursor(0, 2);
                lcd.printf("Error count: %d      ", errCnt);
            }
            lcd.setCursor(0, 3);
            lcd.print("Up: Exit            ");
    }
}

void Diagnostics::showConfirmMessage() {
    showConfirmMessage(2, 3);
}
void Diagnostics::showConfirmMessage(uint8_t confirmRow, uint8_t cancelRow) {
    lcd.setCursor(0,confirmRow);
    lcd.print("Up:   Continue      ");
    lcd.setCursor(0, cancelRow);
    lcd.print("Down: Cancel        ");
}

void Diagnostics::testWifi() {
    wifiTestStatus = "running...  ";
    wifiTestComplete = false;

    refreshScreen();

    wifiTestStatus = WiFi.ready() ? "OK!         " : "Failed!     ";
    wifiTestComplete = true;
    refreshScreen();
}

void Diagnostics::testService() {
    serviceTestStatus = "running...  ";
    serviceTestComplete = false;
    refreshScreen();

    bool serviceReady = dataService.ping();

    serviceTestStatus = serviceReady ? "OK!         " : "Failed!     ";
    if (!serviceReady) {
        errCnt = errCnt + 1;
    }
    serviceTestComplete = true;

    refreshScreen();
}

void Diagnostics::testTempProbe() {
    float _temp;
    uint8_t i = 0;
    uint8_t maxRetry = 2;

    tempProbeDriver = String(ds18b20.getChipName());

    if(tempProbeDriver == "") {
        errCnt = errCnt + 1;
    } else {
        do {
            _temp = ds18b20.getTemperature();
        } while (!ds18b20.crcCheck() && maxRetry > i++);

        if (i < maxRetry) {
            tempProbeTemp = ds18b20.convertToFahrenheit(_temp);
        }
        else {
            errCnt = errCnt + 1;
        }
    }

    tempProbeTestComplete = true;

    refreshScreen();
}

void Diagnostics::testHeatOutlet() {
    digitalWrite(P_CTRL_COOL, LOW);
    digitalWrite(P_CTRL_HEAT, HIGH);
}

void Diagnostics::testCoolOutlet() {
    digitalWrite(P_CTRL_COOL, HIGH);
    digitalWrite(P_CTRL_HEAT, LOW);
}