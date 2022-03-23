#ifndef FERM_CTRL_CONSTANTS_H
#define FERM_CTRL_CONSTANTS_H

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

const char EMPTY_ROW[] = "                    ";

#endif