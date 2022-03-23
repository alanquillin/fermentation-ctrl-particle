#ifndef FERM_CTRL_DIAGNOSTICS_H
#define FERM_CTRL_DIAGNOSTICS_H

#include <LiquidCrystal_I2C_Spark.h>
#include <DS18B20.h>
#include "service.h"

enum States {INIT, WIFI, SERVICE, TEMP_PROBE, UP_BTN, DOWN_BTN, SET_BTN, HEAT_OUTLET, COOL_OUTLET, RESULTS};

class Diagnostics {
public:
    Diagnostics(LiquidCrystal_I2C& _lcd, DS18B20& _ds18b20, DataService& _dataService, void (*_onComplete)(), void (*_onCancel)());
    void run();
    void upBtnPressed();
    void downBtnPressed();
    void setBtnPressed();

private:
    uint8_t errCnt;
    LiquidCrystal_I2C lcd;
    DS18B20 ds18b20;
    DataService dataService;
    States state;
    String wifiTestStatus;
    bool wifiTestComplete;
    String serviceTestStatus;
    bool serviceTestComplete;
    String probe;
    double temp;
    String tempProbeDriver;
    double tempProbeTemp;
    bool tempProbeTestComplete;
    Logger logger;

    void (*onComplete)();
    void (*onCancel)();

    void clearScreen();
    void clearLines(uint8_t lines[], uint8_t size);
    void refreshScreen();
    void showConfirmMessage();
    void showConfirmMessage(uint8_t confirmRow, uint8_t cancelRow);
    void setState(States state);
    void testWifi();
    void testService();
    void testTempProbe();
    void testHeatOutlet();
    void testCoolOutlet();
};

#endif