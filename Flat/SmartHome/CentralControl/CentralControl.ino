//Home_Central Control
//Arduino Mega
//Управление домом по GSM.
//Получение сигнала ALARM, VODA1, VODA2 на тел-н
//отправка Tout к устр-м отображения по NRFL
//включение вентилятора по NRFL от устр-в
//useRecallMeMode

// GND VCC CE  CSN MOSI  MISO  SCK
//          9  10  11    12    13
//  http://robotclass.ru/tutorials/arduino-radio-nrf24l01/
//

#include <SoftwareSerial.h>
#include <elapsedMillis.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <mp3TF.h>
#include <EEPROM.h>
#include "sav_button.h" // Библиотека работы с кнопками
#include <RF24.h>
#include <RF24_config.h>
#include "DHT.h"      //влажность
#include <Adafruit_BMP085.h> //давление
#include <Adafruit_SSD1306.h> //дисплей

#include <avr/wdt.h>
#include <avr/power.h>
#include <util/delay.h>

#define SOFT_RX_PIN 2
#define SOFT_TX_PIN 3

#define OLED_RESET 4
#define U_220_PIN 5       // контроль наличия 220в, с конденсатором, чтобы не реагировать на импульсы пропадания
#define ONE_WIRE_PIN 6    // DS18b20
#define ALARM_CHECK_PIN 7 // 1 при срабатывании сигнализации
#define DHT_PIN 8

//RNF SPI bus plus pins  9,10 для Уно или 9, 53 для Меги
#define CE_PIN 9
#define CSN_PIN 53

#define MP3_BUSY_PIN 11    // пин от BUSY плеера
#define BTTN_PIN 12       // ручное управление командами
#define BZZ_PIN 13
#define MIC_PIN 14        // активация микрофона(т.е. переключение на микрофон вместо MP3)
#define REGISTRATOR_PIN 15// активация регистратора
#define ADD_DEVICE_PIN 16 // дополнительное устройство 

#define VENT_SPEED1_PIN 17
#define VENT_SPEED2_PIN 18

#define DHTTYPE DHT22

SButton btnControl(BTTN_PIN, 50, 700, 1500, 15000);

SoftwareSerial mySerialGSM(SOFT_RX_PIN, SOFT_TX_PIN);
SoftwareSerial mySerialMP3(11, 12);

Adafruit_BMP085 bmp;
DHT dht(DHT_PIN, DHTTYPE);

Adafruit_SSD1306 display(OLED_RESET);

// Set up nRF24L01 radio on SPI bus plus pins 9 & 10  9,10 для Уно или 9, 53 для Меги
RF24 radio(CE_PIN, CSN_PIN);
// Single radio pipe address for the 2 nodes to communicate.  Значение "трубы" передатчика и приемника ОБЯЗАНЫ быть одинаковыми.
const uint64_t readingPipe = 0xE8E8F0F0ABLL;
const uint64_t writingPipe = 0xE8E8F0F0AALL;
const uint8_t channelNRF = 0x60;



const unsigned long IN_UNKNOWN_ANSWER_PERIOD_S = 10;
const unsigned long IN_ANSWER_PERIOD_S = 5;
const unsigned long IN_UNKNOWN_CALL_TALK_PERIOD_S = 55;
const unsigned long IN_CALL_TALK_PERIOD_S = 40;
const unsigned long IN_DISCONNECT_PERIOD_S = 5;
const unsigned long OUT_TONE_DISCONNECT_PERIOD_S = 10;
const unsigned long OUT_NO_TONE_DISCONNECT_PERIOD_S = 15;
//const unsigned long RECALL_NOANSWER_DISCONNECT_PERIOD_S = 20; //when recall without answer or without line
const unsigned long OUTGOING_TALK_DISCONNECT_PERIOD_S = 30; //when recall with answer
const unsigned long OUT_INFORM_PERIOD_S = 8;
const unsigned long RECALL_PERIOD_S = 5;
const unsigned long OUT_INFORM_PERIOD_1_S = 2;
const unsigned long OUT_INFORM_PERIOD_2_S = 3;

const unsigned long REFRESH_SENSOR_INTERVAL_S = 100;
const unsigned long BLOCK_UNKNOWN_PHONES_PERIOD_S = 1800; //30min
const unsigned long MAX_ON_PERIOD_ADD_DEVICE_S = 3600; //60min ограничение максимального времени работы доп устройства
const unsigned long ON_PERIOD_REGISTRATOR = 180; //3min времени работы регистратора
const unsigned long VENT_CORRECTION_PERIOD_S = 300; //5min
const unsigned long NAGREV_CONTROL_PERIOD_S = 60;
const unsigned long AUTO_REFRESH_DISPLAY_PERIOD_S = 10;
const unsigned long INPUT_COMMAND_DISPLAY_PERIOD_S = 60;

const byte ROOM_GOST = 0;
const byte ROOM_BED = 1;
const byte ROOM_VANNA1 = 2;
const byte ROOM_VANNA2 = 3;
const byte ROOM_HALL = 4;

//Параметры комфорта
const float MIN_COMFORT_ROOM_TEMP_WINTER = 21.0;
const float MIN_COMFORT_ROOM_TEMP_SUMMER = 21.5;
const float BORDER_WINTER_SUMMER = 5; // +5c
const byte PPM_SWITCH_ON_MAX_VENT = 8; //*100 = 800
const byte PPM_SWITCH_ON_VENT = 5; //*100 = 500
const byte PPM_SWITCH_OFF_VENT = 4;  //*100 = 400

const byte INDEX_ALARM_PNONE = 1;               //index in phonesEEPROM[5]
const byte MAX_NUMBER_ATTEMPTS_UNKNOWN_PHONES = 3;  //После MAX_NUMBER_ATTEMPTS_UNKNOWN_PHONES неудачных попыток (с вводом неверного пароля) за последние 10 мин, блокируем (не берем трубку) звонки с любых неизвестных номеров на 30мин либо до звонка с известного номера (что раньше).
const byte ADR_EEPROM_RECALL_ME = 1;            //useRecallMeMode
const byte ADR_EEPROM_STORED_PHONES = 100;      //начало списка 7значных номеров телефонов (5шт по 11 байт)
const byte ADR_EEPROM_PASSWORD_UNKNOWN_PHONES = 10; //начало пароля для доступа неопознанных тел-в
const byte ADR_EEPROM_PASSWORD_ADMIN = 20;      //начало админского пароля

const byte ADR_EEPROM_SCENARIO1_NAGREV = 100;    //начало адресов температур по комнатам для SCENARIO1_NAGREV; а последнем адресе - период работы сценария, часов
const byte ADR_EEPROM_SCENARIO2_NAGREV = 110;    //начало температур для SCENARIO2_NAGREV
const byte ADR_EEPROM_SCENARIO3_NAGREV = 120;    //начало температур для SCENARIO3_NAGREV

const byte ADR_EEPROM_SCENARIO1_VENT = 200;    //начало адресов вент по комнатам для SCENARIO1_VENT; а последнем адресе - период работы сценария, часов
const byte ADR_EEPROM_SCENARIO2_VENT = 210;    //начало вент для SCENARIO2_VENT
const byte ADR_EEPROM_SCENARIO3_VENT = 220;    //начало вент для SCENARIO3_VENT

bool useRecallMeMode = false;          // true - will recall for receiing DTMF, false - will answer for receiing DTMF
byte incomingPhoneID;
String incomingPhone;
byte ringNumber;
byte numberAttemptsUnknownPhones;

byte stateDTMF;
boolean AlarmMode = false;
boolean bNo220 = false;
String resultFullDTMF = "";                                   // Переменная для хранения вводимых DTMF данных
String resultCommandDTMF = "";                                // Переменная для хранения введенной DTMF команды
String resultPasswordDTMF_1 = "";                               // Переменная для хранения введенного пароля DTMF
String resultPasswordDTMF_2 = "";                          // Переменная для хранения введенного админского пароля DTMF
int addDTMFParam = 0;
unsigned long onPeriodAddDevice_s;
unsigned long displayPeriod = AUTO_REFRESH_DISPLAY_PERIOD_S;
unsigned long scenarioNagrevPeriod;  //период работы сценария нагрева, в часах. Если в EEPRON = 0, то = 24 * 90
unsigned long scenarioVentPeriod;  //период работы сценария Ventilator, в часах. Если в EEPRON = 0, то = 24 * 90


boolean alarmStatus = false;
boolean bOutgoingCallToneStarted = false;

String _response = "";              // Переменная для хранения ответов модуля

enum EnCallInform { CI_NO_220, CI_ALARM1, CI_ALARM2, CI_VODA1, CI_VODA2 };

enum EnModeVent { V_TO_AUTO, V_AUTO_SPEED1, V_AUTO_SPEED2, V_AUTO_OFF, V_TO_SPEED1, V_TO_SPEED2, V_SPEED1, V_SPEED2, V_TO_OFF, V_OFF };

enum EnMP3Mode {
  M_NO, M_ASK_DTMF, M_ASK_PASSWORD, M_RECALL_MODE_CHANGE, M_DTMF_RECOGN, M_DTMF_NO_RECOGN, M_DTMF_INCORRECT_PASSWORD, M_COMMAND_APPROVED,
  M_NO_220, M_BREAK_220, M_ALARM1, M_ALARM2,
} mp3Mode = M_NO;

enum EnDTMFCommandMode { DC_WAIT, DC_RECEIVING, DC_WRONG_COMMAND, DC_RECOGNISED_COMMAND, DC_WAIT_CONFIRMATION, DC_CONFIRMED, DC_EXECUTED, DC_REJECTED } dtmfCommandMode = DC_WAIT;

enum EnDTMF {
  D_SAY_NON_ADMIN_COMMANDS, D_SAY_ALL_SETTINGS, D_DO_ALARM, D_REQUEST_HOME_INFO,
  D_SWITCH_ON_REGISTRATOR, D_SWITCH_ON_MIC, D_SWITCH_ON_ADD_DEViCE, D_SWITCH_OFF_ADD_DEViCE,
  D_RESET_NAGREV, D_SCENARIO_NAGREV,
  D_RESET_VENT, D_SCENARIO_VENT,
  D_ADD_THIS_PHONE, D_CHANGE_UNP_PASSWORD, D_CHANGE_ADMIN_PASSWORD, D_CHANGE_RECALL_MODE
} dtmfCommand;

enum EnAddDeviceMode { ADD_DEVICE_OFF, REQUEST_ADD_DEVICE_ON, ADD_DEVICE_ON, REQUEST_ADD_DEVICE_OFF } addDeviceMode = ADD_DEVICE_OFF;

enum EnNagrevMode { NAGREV_OFF, REQUEST_RESET_NAGREV, REQUEST_SCENARIO1_NAGREV, REQUEST_SCENARIO2_NAGREV, REQUEST_SCENARIO3_NAGREV, SCENARIO1_NAGREV, SCENARIO2_NAGREV, SCENARIO3_NAGREV } nagrevMode = NAGREV_OFF;
enum EnVentMode { VENT_OFF, REQUEST_RESET_VENT, REQUEST_SCENARIO1_VENT, REQUEST_SCENARIO2_VENT, REQUEST_SCENARIO3_VENT, SCENARIO1_VENT, SCENARIO2_VENT, SCENARIO3_VENT } ventMode = VENT_OFF;

enum EnMicMode { MIC_OFF, REQUEST_MIC_ON, MIC_ON } micMode = MIC_OFF;

//enum EnGsmMode {
//  WAIT, INCOMING_UNKNOWN_CALL_START, INCOMING_CALL_START, INCOMING_CALL_PROGRESS, INCOMING_UNKNOWN_CALL,
//  INCOMING_CALL_ANSWERED, INCOMING_UNKNOWN_CALL_ANSWERED,
//  INCOMING_CALL_DISCONNECTED, INCOMING_CALL_HANGUP,
//  TODO_CALL,
//  RECALL_DIALING, OUTGOING_CALL_PROGRESS, OUTGOING_CALL_PROGRESS, OUTGOING_TALK, RECALL_HANGUP, RECALL_NOANSWER, RECALL_BUSY,
//  WAIT_PSWD
//} gsmMode = WAIT_GSM;

enum EnGSMSubMode {
  WAIT_GSM_SUB,
  INCOMING_UNKNOWN_CALL,
  CONFIRM_CALL,
  RECALL,
  START_INFO_CALL,
  FINISH_INFO_CALL,
} gsmSubMode = WAIT_GSM_SUB;

enum EnGSMMode {
  WAIT_GSM, INCOMING_CALL_START, INCOMING_CALL_PROGRESS, INCOMING_CALL_DISCONNECTED, INCOMING_CALL_HANGUP, INCOMING_CALL_ANSWERED,
  TODO_CALL, OUTGOING_CALL_PROGRESS, OUTGOING_CALL_HANGUP, OUTGOING_CALL_NOANSWER, OUTGOING_CALL_ANSWERED, OUTGOING_TALK, OUTGOING_CALL_BUSY
} gsmMode = WAIT_GSM;

enum EnDoAlarmMode { ALARM_OFF, REQUEST_ALARM_ON } doAlarmMode = ALARM_OFF;
enum EnOpenDoorMode { OPEN_DOOR_OFF, REQUEST_OPEN_DOOR_ON } openDoorMode = OPEN_DOOR_OFF;
enum EnRegistratorMode { REGISTRATOR_OFF, REQUEST_REGISTRATOR_ON, REGISTRATOR_ON } registratorMode = REGISTRATOR_OFF;
enum EnSendSMSMode { SMS_NO, REQUEST_GPS_SMS, REQUEST_CAR_INFO_SMS } sendSMSMode = SMS_NO;
enum EnWorkEEPROM { EE_PHONE_NUMBER, EE_PASSWORD_UNKNOWN_PHONES, EE_PASSWORD_ADMIN, EE_USE_RECALL_ME, EE_ADD_THIS_PHONE_TO_STORED, EE_SCENARIO_1_NAGREV, EE_SCENARIO_2_NAGREV, EE_SCENARIO_3_NAGREV, EE_SCENARIO_1_VENT, EE_SCENARIO_2_VENT, EE_SCENARIO_3_VENT };
enum enOutCommand { OUT_NO, OUT_T_INFO, OUT_CLOSE_VODA_1, OUT_CLOSE_VODA_2 } outCommand = OUT_NO;
enum enInCommand { IN_NO, IN_ROOM_INFO, IN_ROOM_COMMAND, IN_CENTRAL_COMMAND } inCommand = IN_NO;
enum enAlarmType { ALR_NO, ALR_VODA, ALR_DOOR };

float t_inn[5];       //температура внутри, по комнатам
byte h[5];          //влажность внутри, по комнатам
int co2[5];         //co2 по комнатам
float t_set[5];         //желаемая температура по комнатам
float vent_set[5];         //желаемая вентиляция по комнатам
boolean nagrevStatus[5];      //состояние батарей по комнатам (true/false)
EnModeVent modeVent[5];   //вентиляция по комнатам


float t_out1;   //температура снаружи место1
float t_out2;   //температура снаружи место2
float t_out;   //температура снаружи (минимальная место1 или место2)
float t_vent;   //температура внутри вентиляционной системы (в блоке разветвления воздуха) для расчета (t_vent - t_out)
float t_unit;   //температура блока управления (в самой горячей точке)
float t_bat;    //температура батареи отопления (ближайшей)
int p_v = 0;    //давление
byte h_out = 0;   //влажность снаружи


elapsedMillis inCallReaction_ms;
elapsedMillis recallNoAnswerDisconnect_ms;
elapsedMillis recallTalkDisconnect_ms;
elapsedMillis recallPeriod_ms;
elapsedMillis inCallTalkPeriod_ms;
elapsedMillis lastRefreshSensor_ms = REFRESH_SENSOR_INTERVAL_S * 1000;
elapsedMillis blockUnknownPhones_ms;
elapsedMillis ventCorrectionPeriod_ms;
elapsedMillis nagrevControlPeriod_ms;
elapsedMillis displayData_ms = AUTO_REFRESH_DISPLAY_PERIOD_S * 1000;
elapsedMillis outCallStarted_ms;
elapsedMillis outToneStartedDisconnect_ms;

OneWire ds(ONE_WIRE_PIN);
DallasTemperature sensors(&ds);
DeviceAddress innerTempDeviceAddress;
DeviceAddress outer1TempDeviceAddress;
DeviceAddress outer2TempDeviceAddress;
DeviceAddress ventTempDeviceAddress;
DeviceAddress unitTempDeviceAddress;

mp3TF mp3tf = mp3TF();

unsigned long c = 0;
unsigned long ca = 0;

String sSoftSerialData = "";

typedef struct {
  enOutCommand Command;
  byte roomNumber; //? или лучше менять address?
  float tOut;
  int p_v;
  boolean nagrevStatus;
} OutNRFCommand;

typedef struct {
  enInCommand Command;
  byte roomNumber;  //1,2,3,4,.. 0-центральное упр-е (Command=IN_CENTRAL_COMMAND)
  float t;
  int co2;
  byte h;
  enAlarmType alarmType;  //voda/door alarm/...
  byte ventSpeed;     //0-not supported, 1-1st speed, 2-2nd speed, 10 - off, 100 - auto
  float t_set;      //желаемая температура (-100 если не задано)
  byte scenarioVent;    //для команды из центрального упр-я 0/1/2/3/99  99 - nothing do
  byte scenarioNagrev;  //для команды из центрального упр-я 0/1/2/3/99  99 - nothing do
} InNRFCommand;

OutNRFCommand outNRFCommand;
InNRFCommand inNRFCommand;
boolean nrfCommandProcessing = false; //true when received nrf command


void setup()
{
  //Initialize serial ports for communication.
  Serial.begin(9600);
  mySerialGSM.begin(9600);
  mySerialMP3.begin(9600);

  Serial.println("Setup start");


  // первый раз, потом закоментарить
  //InitialEepromSettings();

  pinMode(ALARM_CHECK_PIN, INPUT_PULLUP);
  pinMode(U_220_PIN, INPUT);
  pinMode(BTTN_PIN, INPUT_PULLUP);
  pinMode(BZZ_PIN, OUTPUT);
  pinMode(ADD_DEVICE_PIN, OUTPUT);
  pinMode(MIC_PIN, OUTPUT);
  pinMode(REGISTRATOR_PIN, OUTPUT);

  useRecallMeMode = (EEPROM.read(ADR_EEPROM_RECALL_ME) == 49);
  Serial.print("useRecallMeMode= ");
  if (useRecallMeMode)
    Serial.println("tr ue");
  else
    Serial.println("false");

  cellSetup();

  RadioSetup();

  dht.begin();
  bmp.begin();

  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3D (for the 128x64)

  //mp3
  mp3tf.init(&mySerialMP3);
  _delay_ms(200);
  pinMode(MP3_BUSY_PIN, INPUT);
  mp3tf.volumeSet(15);
  _delay_ms(200);
  ozv(1, 1, false);

  // Инициация кнопок
  btnControl.begin();

  sensors.begin();
  sensors.getAddress(innerTempDeviceAddress, 0);
  sensors.getAddress(outer1TempDeviceAddress, 1);
  sensors.getAddress(outer2TempDeviceAddress, 2);
  sensors.getAddress(ventTempDeviceAddress, 3);
  sensors.getAddress(unitTempDeviceAddress, 4);
  sensors.setResolution(innerTempDeviceAddress, 11);
  sensors.setResolution(outer1TempDeviceAddress, 11);
  sensors.setResolution(outer2TempDeviceAddress, 11);
  sensors.setResolution(ventTempDeviceAddress, 11);
  sensors.setResolution(unitTempDeviceAddress, 11);

  digitalWrite(BZZ_PIN, HIGH);
  _delay_ms(2000);
  digitalWrite(BZZ_PIN, LOW);

  for (byte i = 0; i < 5; i++)
  {
    modeVent[i] = V_AUTO_OFF;
  }

  Serial.println("Setup done");

  wdt_enable(WDTO_8S);
}

void RadioSetup()
{
  //RF24
  //стоило переключить CS с 9 ножки в + и все бодренько заработало

  radio.begin();                          // Включение модуля;
  _delay_ms(2);
  //radio.enableAckPayload();       //+
  radio.setPayloadSize(8);
  radio.setChannel(channelNRF);            // Установка канала вещания;
  radio.setRetries(10, 10);                // Установка интервала и количества попыток "дозвона" до приемника;
  radio.setDataRate(RF24_1MBPS);        // Установка скорости(RF24_250KBPS, RF24_1MBPS или RF24_2MBPS), RF24_250KBPS на nRF24L01 (без +) неработает.
  radio.setPALevel(RF24_PA_MAX);          // Установка максимальной мощности;
  //radio.setAutoAck(0);                    // Установка режима подтверждения приема;
  radio.openWritingPipe(writingPipe);     // Активация данных для отправки
  radio.openReadingPipe(1, readingPipe);   // Активация данных для чтения
  radio.startListening();

  radio.printDetails();
}

void cellSetup()
{
  //_delay_ms(15000);
  mySerialGSM.println("AT");
  _delay_ms(100);
  mySerialGSM.println("AT+DDET=1,100,0,0");
  _delay_ms(1000);
  mySerialGSM.println("AT+CLIP=1");
  _delay_ms(1000);
  mySerialGSM.println("AT+CSCLK=2");
  _delay_ms(1000);
  //установить длительность исходящих DTMF
  mySerialGSM.println("AT+VTD=3");
  _delay_ms(300);
  //скорость Serial port
  //mySerialGSM.println("AT+IPR=19200");
  //_delay_ms(300);
  //чувствительность микрофона
  //  mySerialGSM.println("AT+CMIC=0, 9");
  //  _delay_ms(300);

  //  //Брать время gsm сети (при перегистрации, но работает не у всех операторов)
  //  mySerialGSM.println("AT+CLTS=1");
  //  _delay_ms(300);
  //сохранить настройки
  //  mySerialGSM.println("AT&W");
  //  _delay_ms(300);

  SIMflush();

  mySerialGSM.println("AT");
  _delay_ms(100);
}

void SIMflush()
{
  Serial.println("SIMflush_1");
  while (mySerialGSM.available() != 0)
  {
    mySerialGSM.read();
  }
  Serial.println("SIMflush_2");
}

//1й старт, потом закоментарить
void InitialEepromSettings()
{
  //4 set recallMe mode=1
  workWithEEPROM(1, EE_USE_RECALL_ME, 0, "1");
  //5 add my phone numbers
  workWithEEPROM(1, EE_ADD_THIS_PHONE_TO_STORED, 0, "79062656420");
  workWithEEPROM(1, EE_ADD_THIS_PHONE_TO_STORED, 1, "79990424298");
  //2 set unknown psswd
  workWithEEPROM(1, EE_PASSWORD_UNKNOWN_PHONES, 0, "1234");
  //3 set admin psswd
  workWithEEPROM(1, EE_PASSWORD_ADMIN, 0, "4321");
}

void CheckAlarmLine()
{
  alarmStatus = digitalRead(ALARM_CHECK_PIN);  //store new condition
}

void SendSMS(String text, String phone)
{
  Serial.println("SMS send started");
  Serial.println("phone:" + phone);
  Serial.println("text:" + text);
  /*mySerialGSM.print("AT+CMGS=\"");
    mySerialGSM.print("+" + phone);
    mySerialGSM.println("\"");
    _delay_ms(1000);
    mySerialGSM.print(text);

    _delay_ms(500);

    mySerialGSM.write(0x1A);
    mySerialGSM.write(0x0D);
    mySerialGSM.write(0x0A);*/
  Serial.println("SMS send finish");
}

void DoCall(boolean callToMyPhone)
{
  String sPhone;
  if (callToMyPhone)
    sPhone = workWithEEPROM(0, EE_PHONE_NUMBER, 0, "");
  else
    sPhone = incomingPhone;
  SIMflush();
  Serial.println("Call to " + sPhone);
  mySerialGSM.println("AT");
  _delay_ms(100);
  mySerialGSM.println("ATD+" + sPhone + ";"); //позвонить
  _delay_ms(500);

  outCallStarted_ms = 0;
  bOutgoingCallToneStarted = false;
  gsmMode = OUTGOING_CALL_PROGRESS;
  /*ATD + 790XXXXXXXX; Позвонить на номер + 790XXXXXXXX;
    NO DIALTONE Нет сигнала
    BUSY  Если вызов отклонён
    NO CARRIER Повесили трубку
    NO ANSWER Нет ответа*/
}

//mp3Mode
void sayInfo()
{
  Serial.print("sayInfo:");
  Serial.println(mp3Mode);
  //Serial.println(millis());
  ozv(1, mp3Mode, false);
  //Serial.println(millis());
  //Serial.println("sayInfoEnd");
  mp3Mode = M_NO;
}

//воспроизвести из фолдера № fld файл № file. Ждать окончания воспроизведения если playToEnd=true
void ozv(int fld, int file, boolean playToEnd)
{
  mp3tf.playFolder2(fld, file);
  mySerialMP3.println("AT");
  _delay_ms(100);
  while (playToEnd && !digitalRead(MP3_BUSY_PIN));
}

void sayCommandsList(byte typeCommands) //typeCommands = 2 - commands; 3 - settngs
{
  //  for (int i = 0; 10; i++)
  //  {
  //    mp3tf.playFolder2(typeCommands, i);
  //    _delay_ms(200);
  //    while (!digitalRead(MP3_BUSY_PIN));
  //  }
}

void InformCall(EnCallInform typeCallInform) //CI_NO_220, CI_BREAK_220, CI_ALARM
{
  DoCall(true);
  switch (typeCallInform)
  {
    case CI_NO_220:
      mp3Mode = M_NO_220;
      break;
    case CI_ALARM1:
      mp3Mode = M_ALARM1;
      break;
  }
  sayInfo();
}


String workWithEEPROM(byte mode, EnWorkEEPROM dataType, int addParam, byte* arrDataToSave)  //mode = 0- read, 1- write;
//sDataSave - string to save (for mode=1);
//dataType = EE_PHONE_NUMBER, EE_PASSWORD_UNKNOWN_PHONES, EE_PASSWORD_ADMIN, EE_USE_RECALL_ME, EE_ADD_THIS_PHONE_TO_STORED, EE_SCENARIO_1_NAGREV, EE_SCENARIO_2_NAGREV
{
  //Serial.println("workWithEEPROM");
  byte* result;
  int adrEEPROM;
  int numSymbols;
  char buf[11];
  switch (dataType)
  {
    case EE_PHONE_NUMBER:
    case EE_ADD_THIS_PHONE_TO_STORED:
      adrEEPROM = ADR_EEPROM_STORED_PHONES;
      numSymbols = 11;
      break;
    case EE_PASSWORD_UNKNOWN_PHONES:
      adrEEPROM = ADR_EEPROM_PASSWORD_UNKNOWN_PHONES;
      numSymbols = 4;
      break;
    case EE_PASSWORD_ADMIN:
      adrEEPROM = ADR_EEPROM_PASSWORD_ADMIN;
      numSymbols = 4;
      break;
    case EE_USE_RECALL_ME:
      adrEEPROM = ADR_EEPROM_RECALL_ME;
      numSymbols = 1;
      break;
    case EE_SCENARIO_1_NAGREV:
    case EE_SCENARIO_2_NAGREV:
    case EE_SCENARIO_3_NAGREV:
      adrEEPROM = (dataType == EE_SCENARIO_1_NAGREV ? ADR_EEPROM_SCENARIO1_NAGREV : dataType == EE_SCENARIO_2_NAGREV ? ADR_EEPROM_SCENARIO2_NAGREV : ADR_EEPROM_SCENARIO3_NAGREV);
      numSymbols = 1;
      break;
    case EE_SCENARIO_1_VENT:
    case EE_SCENARIO_2_VENT:
    case EE_SCENARIO_3_VENT:
      adrEEPROM = (dataType == EE_SCENARIO_1_VENT ? ADR_EEPROM_SCENARIO1_VENT : dataType == EE_SCENARIO_2_VENT ? ADR_EEPROM_SCENARIO2_VENT : ADR_EEPROM_SCENARIO3_VENT);
      numSymbols = 1;
      break;
  }

  for (int k = 0; k < numSymbols; k++)
  {
    if (mode == 0)
      buf[k] = EEPROM[k + adrEEPROM + addParam * numSymbols];
    else
      EEPROM.write(k + adrEEPROM + addParam * numSymbols, arrDataToSave[k]);
  }
  return String(buf);
}


boolean CheckPhone(String str)
{
  Serial.print("CheckPh:" + str);
  //  "+CMT: "+79062656420",,"13/04/17,22:04:05+22"    //SMS
  //  "+CLIP: "79062656420",145,,,"",0"                //INCOMING CALL
  incomingPhoneID = 0;
  incomingPhone = "";
  if (str.indexOf("+CLIP:") > -1 || str.indexOf("+CMT:") > -1)
  {
    for (byte i = 0; i < 5; i++)
    {
      String sPhone = workWithEEPROM(0, EE_PHONE_NUMBER, i, "");
      sPhone = sPhone.substring(0, 11);
      //sPhone = "79062656420";
      //      Serial.print("EEPROM phone #: ");
      //      Serial.print(i);
      //      Serial.println(" : " + sPhone);
      if (str.indexOf(sPhone) > -1)
      {
        incomingPhoneID = i + 1;
        incomingPhone = sPhone;
        Serial.print("Matched: ");
        Serial.println(incomingPhone);
        break;
      }
      else
      {
        Serial.print("Not matched: " + sPhone);
      }
    }
  }
  return (incomingPhoneID > 0);
}

bool CheckPassword(byte passwordType, String pwd) //passwordType 2 - for UnknownPhones, 3 - Admin
{
  return (workWithEEPROM(0, passwordType, 0, "") == pwd);
}

//show last command
void DisplayData(int mode) //mode= 0-auto(t out); 1-display last input rnf or dtmf command 1 minute after receive
{
  if (mode == 1)
  {
    displayData_ms = INPUT_COMMAND_DISPLAY_PERIOD_S * 1000 + 1;
    displayPeriod = INPUT_COMMAND_DISPLAY_PERIOD_S;
  }

  if (mode == 1 || mode == 0 && displayData_ms > displayPeriod * 1000)
  {
    display.clearDisplay();

    switch (mode)
    {
      case 0:
        display.setTextSize(3);
        display.setTextColor(WHITE);
        display.setCursor(0, 0);
        //display.setTextColor(BLACK, WHITE); // 'inverted' text

        display.print(t_unit);
        display.print("/");
        display.print(t_out);
        display.setTextColor(WHITE);
        display.setTextSize(2);
        display.println(" c");

        displayPeriod = AUTO_REFRESH_DISPLAY_PERIOD_S;
        displayData_ms = 0;
        break;

      case 1:
        display.setTextSize(2);
        display.print("Room ");
        display.println(inNRFCommand.roomNumber);
        display.print("T ");
        display.println(inNRFCommand.t);
        display.print("CO2 ");
        display.println(inNRFCommand.co2);
        display.print("H ");
        display.println(inNRFCommand.h);
        display.print("DTMF ");
        switch (dtmfCommandMode)
        {
          case D_SAY_NON_ADMIN_COMMANDS:
            display.println("say comms");
          case D_SAY_ALL_SETTINGS:
            display.println("say sett");
          case D_DO_ALARM:
            display.println("alarm");
          case D_REQUEST_HOME_INFO:
            display.println("rqst info");
          case D_SWITCH_ON_REGISTRATOR:
            display.println("reg on");
          case D_SWITCH_ON_MIC:
            display.println("mic on");
          case D_SWITCH_ON_ADD_DEViCE:
            display.println("add device");
          case D_RESET_NAGREV:
            display.println("rst nagrev");
          case D_SCENARIO_NAGREV:
            display.println("nagrev on");
        }
        break;
    }

    display.display();
  }
}

//String getHomeInfo()
//{
//  String sResult = "";
//  sResult += " Tout= " + String(t_out);
//  sResult += " Tinn= " + String(t_inn);
//  sResult += " Tunit= " + String(t_unit);
//  return sResult;
//}

void PrepareDtmfCommand(byte* arrDataToSave)  //enum EnDTMF { D_ON_HEAT, D_OFF_HEAT, D_INFO_HEAT, D_REQUEST_HOME_INFO, D_OPEN_DOOR, D_CHANGE_UNP_PASSWORD, D_CHANGE_ADMIN_PASSWORD, D_RECALL_MODE_CHANGE, D_REQUEST_GPS_SMS };
{
  switch (dtmfCommand)
  {
    case D_SAY_NON_ADMIN_COMMANDS:
      sayCommandsList(2);
      break;
    case D_SAY_ALL_SETTINGS:
      sayCommandsList(3);
      break;
    case D_REQUEST_HOME_INFO:
      sendSMSMode = REQUEST_CAR_INFO_SMS;
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      break;
    case D_SWITCH_ON_REGISTRATOR:
      registratorMode = REQUEST_REGISTRATOR_ON;
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      Serial.println("Reg");
      break;
    case D_SWITCH_ON_MIC:
      Serial.println("MIC_ON");
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      micMode = REQUEST_MIC_ON;
      break;
    case D_DO_ALARM:
      Serial.println("D_DO_ALARM");
      doAlarmMode = REQUEST_ALARM_ON;
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      break;
    case D_SWITCH_ON_ADD_DEViCE:
      addDeviceMode = REQUEST_ADD_DEVICE_ON;
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      break;
    case D_SWITCH_OFF_ADD_DEViCE:
      addDeviceMode = REQUEST_ADD_DEVICE_OFF;
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      break;

    case D_RESET_NAGREV:
      nagrevMode = REQUEST_RESET_NAGREV;
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      break;
    case D_SCENARIO_NAGREV:
      switch (addDTMFParam)
      {
        case 1:
          nagrevMode = REQUEST_SCENARIO1_NAGREV;
          break;
        case 2:
          nagrevMode = REQUEST_SCENARIO2_NAGREV;
          break;
        case 3:
          nagrevMode = REQUEST_SCENARIO3_NAGREV;
          break;
      }
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      break;

    case D_RESET_VENT:
      ventMode = REQUEST_RESET_VENT;
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      break;
    case D_SCENARIO_VENT:
      switch (addDTMFParam)
      {
        case 1:
          ventMode = REQUEST_SCENARIO1_VENT;
          break;
        case 2:
          ventMode = REQUEST_SCENARIO2_VENT;
          break;
        case 3:
          ventMode = REQUEST_SCENARIO3_VENT;
          break;
      }
      dtmfCommandMode = DC_WAIT_CONFIRMATION;
      break;

    case D_CHANGE_UNP_PASSWORD:
    case D_CHANGE_ADMIN_PASSWORD:
    case D_CHANGE_RECALL_MODE:
    case D_ADD_THIS_PHONE:
      workWithEEPROM(1, (dtmfCommand == D_CHANGE_UNP_PASSWORD ? EE_PASSWORD_UNKNOWN_PHONES :
                         (dtmfCommand == D_CHANGE_ADMIN_PASSWORD ? EE_PASSWORD_ADMIN :
                          (dtmfCommand == D_CHANGE_RECALL_MODE ? EE_USE_RECALL_ME : EE_ADD_THIS_PHONE_TO_STORED))), 0, arrDataToSave);
      Serial.println("eeprom saved");
      break;
  }
}


// Parsing DTMF команд, вызов PrepareDtmfCommand()
void CheckDTMF(String symbol)
{
  Serial.println("Key: " + symbol);                         // Выводим в Serial для контроля, что ничего не потерялось
  ozv(1, 1, false);
  if (dtmfCommandMode == DC_WAIT_CONFIRMATION)
  {
    if (symbol == "#") //это пришла команда-подверждение - "#" (т.е. команда из единственного символа- "#")
    {
      ozv(1, 7, false);
      Serial.println("DC_CONFIRMED");
      dtmfCommandMode = DC_CONFIRMED;
    }
    else //ожидали команды-подверждения, а пришел не "#" - отбой
    {
      Serial.println("DC_REJECTED");
      dtmfCommandMode = DC_REJECTED;
    }
    resultFullDTMF = "";                                      // сбрасываем вводимую комбинацию
  }
  else if (symbol == "#")                                        //признак завершения ввода команды, начинаем парсинг и проверку
  {
    ozv(1, 6, false);

    bool correct = true;                                   // Для оптимизации кода, переменная корректности команды
    ParsingDTMF();
    // Serial.println("ParsingDTMF_end");
    byte* arrDataToSave = "";  //данные для сохранения в EEPROM
    //      Serial.print("resultCommandDTMF[0]: ");
    //      Serial.println((int)resultCommandDTMF[0] - 48);
    //      Serial.print("resultCommandDTMF[1]: ");
    //      Serial.println((int)resultCommandDTMF[1] - 48);
    switch ((int)resultCommandDTMF[0] - 48)
    {
      case 0:
        // Serial.println("0 - проговорить все доступные DTMF команды (кроме настроечных)");
        dtmfCommand = D_SAY_NON_ADMIN_COMMANDS;
        break;
      case 1:
        // Serial.println("1 - получить сообщение о состоянии ALARM, подогрева, температуры, напряжения,..");
        dtmfCommand = D_REQUEST_HOME_INFO;
        break;
      case 2:
        // Serial.println("20 - выключить все нагревы; 21/22/23/... - запустить сценарий нагрева №0/1/2,3...
        if (((int)resultCommandDTMF[1] - 48) == 0)
        {
          dtmfCommand = D_RESET_NAGREV;
        }
        else
        {
          dtmfCommand = D_SCENARIO_NAGREV;
          addDTMFParam = (int)resultCommandDTMF[1] - 48;
        }
        break;
      case 3:
        // Serial.println("20 - выключить все vent; 21/22/23/... - запустить сценарий vent №0/1/2,3...
        if (((int)resultCommandDTMF[1] - 48) == 0)
        {
          dtmfCommand = D_RESET_VENT;
        }
        else
        {
          dtmfCommand = D_SCENARIO_VENT;
          addDTMFParam = (int)resultCommandDTMF[1] - 48;
        }
        break;
      case 4:
        // Serial.println("3 - сработать сигнализации");
        dtmfCommand = D_DO_ALARM;
        break;
      //case 4:
      //  // 4219 - включить обогрев в Room № 2 на 19 С
      //  dtmfCommand = D_OPEN_DOOR;
      //  break;
      case 5:
        //Serial.println("5 - включить видеорегистратор (на 3 мин).");
        dtmfCommand = D_SWITCH_ON_REGISTRATOR;
        break;
      case 6:
        //Serial.println("61, 60 - включить/выключить доп устройсво №1;
        // 62437 - включить доп устройсво на 437 минут (62 - начало команды с временем)
        switch ((int)resultCommandDTMF[1] - 48)
        {
          case 1:
            dtmfCommand = D_SWITCH_ON_ADD_DEViCE;
            break;
          case 2:
            //addDTMFParam = ((int)resultCommandDTMF[resultCommandDTMF.length - 3] - 48) * 1000 + ((int)resultCommandDTMF[resultCommandDTMF.length - 2] - 48) * 100 + ((int)resultCommandDTMF[resultCommandDTMF.length - 1] - 48);
            addDTMFParam = 3;
            dtmfCommand = D_SWITCH_ON_ADD_DEViCE;
            break;
          default:
            dtmfCommand = D_SWITCH_OFF_ADD_DEViCE;
            break;
        }
        break;
      case 7:
        //включить микрофон
        dtmfCommand = D_SWITCH_ON_MIC;
        break;

      //Настроечные команды - resultCommandDTMF[0]=9, после команда* всегда идет AdminPWD, т.е. в resultPasswordDTMF_1
      case 9:
        switch (((int)resultCommandDTMF[1] - 48))
        {
          case 0:
            // Serial.println("0 - проговорить все доступные настроечные DTMF команды");
            dtmfCommand = D_SAY_ALL_SETTINGS;
            break;
          case 1:
            dtmfCommand = D_ADD_THIS_PHONE;
            incomingPhone.toCharArray(arrDataToSave, 11);
            break;
          case 2:
            dtmfCommand = D_CHANGE_UNP_PASSWORD;
            resultPasswordDTMF_2.toCharArray(arrDataToSave, 4);
            break;
          case 3:
            dtmfCommand = D_CHANGE_ADMIN_PASSWORD;
            resultPasswordDTMF_2.toCharArray(arrDataToSave, 4);
            break;
          case 4:
            dtmfCommand = D_CHANGE_RECALL_MODE;
            if (((int)resultCommandDTMF[2] - 48) == 1)
              arrDataToSave = "1";
            else
              arrDataToSave = "0";
            break;
          default:
            correct = false;
            break;
        }
        if (correct)
        {
          if (!CheckPassword(3, resultPasswordDTMF_1)) //пароль AP неверен
          {
            Serial.println("Incorrect APWD");
            mp3Mode = M_DTMF_INCORRECT_PASSWORD;
            dtmfCommandMode = DC_WRONG_COMMAND;
            correct = false;
          }
        }
      default:
        correct = false;
        break;
    }
    if (correct && gsmSubMode == INCOMING_UNKNOWN_CALL)
    {
      if (!CheckPassword(2, resultPasswordDTMF_1)) //пароль для UP неверен
      {
        Serial.println("Incorrect UPPWD");
        mp3Mode = M_DTMF_INCORRECT_PASSWORD;
        dtmfCommandMode = DC_WRONG_COMMAND;
        correct = false;
      }
    }
    if (correct)
    {
      Serial.println("RECOGNISED");
      dtmfCommandMode = DC_RECOGNISED_COMMAND;
      ozv(1, 8, false);
      mp3Mode = M_DTMF_RECOGN;
      PrepareDtmfCommand(arrDataToSave);
      DisplayData(1);
    }
    else // Если команда нераспознана, выводим сообщение
    {
      Serial.println("DC_WRONG_COMMAND");
      dtmfCommandMode = DC_WRONG_COMMAND;
      mp3Mode = M_DTMF_NO_RECOGN;
      // Serial.println("Wrong command: " + resultFullDTMF);
    }
    resultFullDTMF = "";                                      // После каждой решетки сбрасываем вводимую комбинацию
  }

  else  //"#" еще нет, продолжаем посимвольно собирать команду
  {
    resultFullDTMF += symbol;
    dtmfCommandMode = DC_RECEIVING;
  }
}

void ParsingDTMF()
{
  byte posEndCommand;
  byte posEndPassword_1;
  byte posEndPassword_2;
  //Serial.println("ParsingDTMF: " + resultFullDTMF);
  if (resultFullDTMF.indexOf("*") == -1) //only command, without password
  {
    posEndCommand = resultFullDTMF.indexOf("#");
  }
  else  //command and password(s)
  {
    posEndCommand = resultFullDTMF.indexOf("*");
    if (resultFullDTMF.substring(posEndCommand + 1).indexOf("*") != -1) // 2 passwords
    {
      posEndPassword_1 = posEndCommand + 1 + resultFullDTMF.substring(posEndCommand + 1).indexOf("*");
      posEndPassword_2 = posEndPassword_1 + 1 + resultFullDTMF.substring(posEndPassword_1 + 1).indexOf("#");
    }
    else  //only 1 password
    {
      posEndPassword_1 = posEndCommand + 1 + resultFullDTMF.substring(posEndCommand + 1).indexOf("#");
    }
  }

  addDTMFParam = 0;
  resultCommandDTMF = resultFullDTMF.substring(0, posEndCommand);
  if (posEndPassword_1 > 0) resultPasswordDTMF_1 = resultFullDTMF.substring(posEndCommand + 1, posEndPassword_1);
  if (posEndPassword_2 > 0) resultPasswordDTMF_2 = resultFullDTMF.substring(posEndPassword_1 + 1, posEndPassword_2);
  Serial.println("CMD: " + resultCommandDTMF);
  Serial.println("PWD1: " + resultPasswordDTMF_1);
  Serial.println("PWD2: " + resultPasswordDTMF_2);
}

void GetSoftSerialData()
{
  sSoftSerialData = "";
  if (mySerialGSM.available()) //если модуль что-то послал
  {
    char ch = ' ';
    unsigned long start_timeout = millis();            // Start the timer
    const unsigned int time_out_length = 3000; //ms
    while (mySerialGSM.available() && ((millis() - start_timeout) < time_out_length))
    {
      start_timeout = millis();
      ch = mySerialGSM.read();
      if ((int)ch != 17 && (int)ch != 19)
      {
        sSoftSerialData.concat(ch);
      }
      _delay_ms(2);
    }

    sSoftSerialData.trim();
    if (sSoftSerialData != "")
    {
      Serial.print("GSM talk> ");
      Serial.println(sSoftSerialData);
    }
    if ((millis() - start_timeout) < time_out_length)
    {
      Serial.println("timeout");
    }
  }
}

//для ATWIN и для SIM800 алгоритмы определения статуса разные
//SIM800
String GetOutgoingCallStatus(String sRespond) //2 - набираем номер, 3 - идет исходящий вызов, ждем ответа, 0 - сняли трубку, идет разговор
{
  String result = "9";

  sRespond.trim();
  int i = sRespond.indexOf("+CLCC:");
  if (i > -1)
  {
    result = sRespond.substring(i + 11, i + 12);
  }
  Serial.print("Status=");
  Serial.println(result);
  return result;
}

void CheckSoftwareSerialData()
{
  //Serial.println("CheckGSMModule");
  //If a character comes in from the cellular module...
  if (sSoftSerialData != "")
  {
    //    if (sSoftSerialData.indexOf("RING") > -1)
    //    {
    //      ringNumber += 1;
    //    }

    if (gsmMode == WAIT_GSM && sSoftSerialData.indexOf("+CLIP") > -1) //если пришел входящий вызов
    {
      Serial.println("IR!");
      gsmMode = INCOMING_CALL_START;
      if (CheckPhone(sSoftSerialData))  // из текущей строки выберем тел номер. если звонящий номер есть в списке доступных, можно действовать
      {
        Serial.println("IP: " + incomingPhone + " " + incomingPhoneID);
      }
      else
      {
        gsmSubMode = INCOMING_UNKNOWN_CALL;
        Serial.println("UNP");
      }
    }
    else if ((gsmMode == INCOMING_CALL_PROGRESS || gsmMode == INCOMING_CALL_ANSWERED) && sSoftSerialData.indexOf("NO CARRIER") > -1) //если входящий вызов сбросили не дождавшись ответа блока или повесили трубку не дождавшись выполнения команды
    {
      Serial.println("IP NC");
      gsmMode = INCOMING_CALL_HANGUP;
    }
    //else if ((gsmMode == RECALL_DIALING || gsmMode == OUTGOING_CALL_PROGRESS || gsmMode == OUTGOING_CALL_ANSWERED || gsmMode == OUTGOING_TALK) && sSoftSerialData.indexOf("NO CARRIER") > -1) //если исходящий от блока вызов сбросили
    //{
    //  Serial.println("WAIT");
    //  gsmMode = WAIT_GSM;
    //}
    else if (gsmMode == OUTGOING_TALK || gsmMode == INCOMING_CALL_ANSWERED)
    {
      Serial.println("Check DTMF");
      int i = sSoftSerialData.indexOf("MF: "); //DTMF:
      if (i > -1)
      {
        CheckDTMF(sSoftSerialData.substring(i + 4, i + 5));                     // Логику выносим для удобства в отдельную функцию
      }
      //else if (gsmMode == RECALL_DIALING && GetOutgoingCallStatus(sSoftSerialData) == "3") //если пошел исходящмй гудок
      //{
      //  Serial.println("RECALLING");
      //  gsmMode = OUTGOING_CALL_PROGRESS;
      //}
      else if (gsmMode == OUTGOING_CALL_PROGRESS && sSoftSerialData.indexOf("BUSY") > -1)
      {
        Serial.println("Outgoing call is hang up");
        gsmMode = OUTGOING_CALL_HANGUP;
      }
      else if (gsmMode == OUTGOING_CALL_PROGRESS && sSoftSerialData.indexOf("NO ANSWER") > -1) //если на исходящий вызов линия  нет ответа
      {
        Serial.println("Outgoing call - line no answer");
        gsmMode = OUTGOING_CALL_NOANSWER;
      }
      else if (gsmMode == OUTGOING_CALL_PROGRESS && sSoftSerialData.indexOf("NO DIALTONE") > -1) //если нет сигнала
      {
        Serial.println("Outgoing call - line no dialstone");
        gsmMode = OUTGOING_CALL_NOANSWER;
      }


      else if (gsmMode == OUTGOING_CALL_PROGRESS && GetOutgoingCallStatus(sSoftSerialData) == "0") //если на исходящий от блока вызов ответили
      {
        Serial.println("ANSWERED");
        gsmMode = OUTGOING_CALL_ANSWERED;
      }
      else if (gsmMode == OUTGOING_CALL_PROGRESS && !bOutgoingCallToneStarted)
      {
        Serial.println("GetOutgoingCallStatus");
        if (GetOutgoingCallStatus(sSoftSerialData) == "3") //если пошел исходящмй гудок
        {
          Serial.println("Tone");
          bOutgoingCallToneStarted = true;
          outToneStartedDisconnect_ms = 0;
        }
      }
      //}
    }
  }
}

void WorkflowGSM()
{
  switch (gsmMode)
  {
    case WAIT_GSM:
      break;
    case INCOMING_CALL_START:
      inCallReaction_ms = 0;
      gsmMode = INCOMING_CALL_PROGRESS;
      break;
    case INCOMING_CALL_PROGRESS:
      // отвечаем в случае useRecallMeMode-FALSE или звонке с неизвестного номера (через IN_UNKNOWN_ANSWER_PERIOD_S), иначе - перезваниваем
      if (useRecallMeMode && gsmSubMode != INCOMING_UNKNOWN_CALL && inCallReaction_ms > IN_DISCONNECT_PERIOD_S * 1000)
      {
        Serial.println("IN Discon");
        BreakCall();
        gsmMode = INCOMING_CALL_DISCONNECTED;
        recallPeriod_ms = 0;
      }
      else if ((!useRecallMeMode && inCallReaction_ms > IN_ANSWER_PERIOD_S * 1000) || (gsmSubMode == INCOMING_UNKNOWN_CALL && inCallReaction_ms > IN_UNKNOWN_ANSWER_PERIOD_S * 1000))
      {
        Serial.println("IN Answ");
        AnswerCall();
        if (gsmSubMode == INCOMING_UNKNOWN_CALL)
          mp3Mode = M_ASK_PASSWORD;
        else
          mp3Mode = M_ASK_DTMF;
        gsmMode = INCOMING_CALL_ANSWERED;
        inCallTalkPeriod_ms = 0;
      }
      break;
    case INCOMING_CALL_DISCONNECTED:  //only when set useRecallMeMode
      if (recallPeriod_ms > RECALL_PERIOD_S * 1000)
      {
        gsmSubMode = RECALL;
        gsmMode = TODO_CALL;
      }
      break;

    case TODO_CALL:
      switch (gsmSubMode)
      {
        case CONFIRM_CALL:
          Serial.println("CONFIRM_CALL");
          DoCall(false);
          break;
        case START_INFO_CALL:
          Serial.println("START_INFO_CALL");
          DoCall(false);
          break;
        case FINISH_INFO_CALL:
          Serial.println("FINISH_INFO_CALL");
          DoCall(false);
          break;
      }
      break;

    case OUTGOING_CALL_PROGRESS:
      if (!bOutgoingCallToneStarted && outCallStarted_ms > OUT_NO_TONE_DISCONNECT_PERIOD_S * 1000)  //если вызов не пошел (сбой сети или вне зоны)
      {
        Serial.println("OUT without tone signal. Disconnection");
        BreakCall();
        gsmSubMode = WAIT_GSM_SUB;
        gsmMode = WAIT_GSM;
        bOutgoingCallToneStarted = false;
      }
      else
        switch (gsmSubMode)
        {
          case START_INFO_CALL:
          case FINISH_INFO_CALL:
            if (bOutgoingCallToneStarted && outToneStartedDisconnect_ms > gsmSubMode == START_INFO_CALL ? OUT_INFORM_PERIOD_1_S : OUT_INFORM_PERIOD_2_S * 1000)
            {
              Serial.println("Disconnect after 1/2 ring");
              BreakCall();
              gsmSubMode = WAIT_GSM_SUB;
              gsmMode = WAIT_GSM;
              bOutgoingCallToneStarted = false;
            }
            break;
          default:
            if (bOutgoingCallToneStarted && outToneStartedDisconnect_ms > OUT_TONE_DISCONNECT_PERIOD_S * 1000) ////разрываем связь через 20с
            {
              Serial.println("OUT without answer. Disconnection");
              BreakCall();
              //gsmSubMode = WAIT_GSM_SUB;
              gsmMode = OUTGOING_CALL_NOANSWER;
              bOutgoingCallToneStarted = false;
            }
            break;
        }

      if (gsmMode == OUTGOING_CALL_PROGRESS && !bOutgoingCallToneStarted)   //пока не ответили и не истекло время ожидания ответа, вызываем запрос состояния "AT+CLCC" чтобы поймать момент ответа
      {
        mySerialGSM.println("AT");
        _delay_ms(200);
        Serial.println("AT+CLCC");
        mySerialGSM.println("AT+CLCC");
        _delay_ms(500);
      }
      break;
    case OUTGOING_CALL_ANSWERED: //как только ответили, выдадим DTMF
      mp3Mode = M_ASK_DTMF;
      gsmMode = OUTGOING_TALK;
      recallTalkDisconnect_ms = 0;

      /*_delay_ms(1000);
        mySerialGSM.println("AT");
        _delay_ms(100);
        mySerialGSM.println("AT+VTS=\"1\"");
        _delay_ms(1000);*/

      break;

    case INCOMING_CALL_ANSWERED:
      if ((dtmfCommandMode == DC_EXECUTED || dtmfCommandMode == DC_REJECTED) || inCallTalkPeriod_ms > (gsmSubMode == INCOMING_UNKNOWN_CALL ? IN_UNKNOWN_CALL_TALK_PERIOD_S : IN_CALL_TALK_PERIOD_S) * 1000)
      {
        Serial.println("Inc call Discn");
        BreakCall();
        FinalResetStatuses();
      }
      break;

    case OUTGOING_TALK:
      if ((dtmfCommandMode == DC_EXECUTED || dtmfCommandMode == DC_REJECTED) || recallTalkDisconnect_ms > OUTGOING_TALK_DISCONNECT_PERIOD_S * 1000)
      {
        Serial.println("Recall talk Discn");
        BreakCall();
        FinalResetStatuses();
      }
      break;

    case OUTGOING_CALL_HANGUP:
    case OUTGOING_CALL_NOANSWER:
    case OUTGOING_CALL_BUSY:
    case INCOMING_CALL_HANGUP:
      //case INCOMING_CALL_DISCONNECTED:
      FinalResetStatuses();
      break;
  }
}

void SetNagrevScenario()
{
  switch (nagrevMode) //NAGREV_OFF, REQUEST_RESET_NAGREV, REQUEST_SCENARIO1_NAGREV, REQUEST_SCENARIO2_NAGREV, REQUEST_SCENARIO3_NAGREV, SCENARIO1_NAGREV, SCENARIO2_NAGREV, SCENARIO3_NAGREV
  {
    case REQUEST_RESET_NAGREV:
      for (byte i = 0; i < 5; i++)
        t_set[i] = -50;
      break;
    case REQUEST_SCENARIO1_NAGREV: //get T (c) for each room and time period(hours) from EEPROM
    case REQUEST_SCENARIO2_NAGREV:
    case REQUEST_SCENARIO3_NAGREV:
      for (byte i = 0; i < 5; i++)
      {
        t_set[i] = workWithEEPROM(0, nagrevMode == REQUEST_SCENARIO1_NAGREV ? EE_SCENARIO_1_NAGREV : nagrevMode == REQUEST_SCENARIO2_NAGREV ? EE_SCENARIO_2_NAGREV : EE_SCENARIO_3_NAGREV, i, "").toInt(); //= 12.5;
        if (t_set[i] == 255)
          t_set[i] = -50;
      }
      scenarioNagrevPeriod = workWithEEPROM(0, EE_SCENARIO_1_NAGREV, 9, "").toInt();
      if (scenarioNagrevPeriod == 0)
        scenarioNagrevPeriod = 24 * 90;
      break;
  }
}

void SetVentScenario()
{
  switch (ventMode) //VENT_OFF, REQUEST_RESET_VENT, REQUEST_SCENARIO1_VENT, REQUEST_SCENARIO2_VENT, REQUEST_SCENARIO3_VENT, SCENARIO1_VENT, SCENARIO2_VENT, SCENARIO3_VENT
  {
    case REQUEST_RESET_VENT:
      for (byte i = 0; i < 5; i++)
        vent_set[i] = -50;
      break;
    case REQUEST_SCENARIO1_VENT: //get vent интенсивность (0/1/2/3) for each room and time period(hours) from EEPROM
    case REQUEST_SCENARIO2_VENT:
    case REQUEST_SCENARIO3_VENT:
      for (byte i = 0; i < 5; i++)
      {
        vent_set[i] = workWithEEPROM(0, ventMode == REQUEST_SCENARIO1_VENT ? EE_SCENARIO_1_VENT : ventMode == REQUEST_SCENARIO2_VENT ? EE_SCENARIO_2_VENT : EE_SCENARIO_3_VENT, i, "").toInt(); //= 12.5;
        if (vent_set[i] == 255)
          vent_set[i] = -50;
      }
      scenarioVentPeriod = workWithEEPROM(0, EE_SCENARIO_1_VENT, 9, "").toInt();
      if (scenarioVentPeriod == 0)
        scenarioVentPeriod = 24 * 90;
      break;
  }
}


void WorkflowMain(byte mode) //0-auto(from loop), 1-manual
{
  if (mp3Mode != M_NO)
  {
    sayInfo();
  }

  if (dtmfCommandMode == DC_WRONG_COMMAND)
  {
    BreakCall();
    FinalResetStatuses();
  }

  if ((nagrevMode == REQUEST_RESET_NAGREV ||
       nagrevMode == REQUEST_SCENARIO1_NAGREV ||
       nagrevMode == REQUEST_SCENARIO2_NAGREV ||
       nagrevMode == REQUEST_SCENARIO3_NAGREV) && dtmfCommandMode == DC_CONFIRMED || nrfCommandProcessing)

  {
    SetNagrevScenario();
    switch (nagrevMode) //NAGREV_OFF, REQUEST_RESET_NAGREV, REQUEST_SCENARIO1_NAGREV, REQUEST_SCENARIO2_NAGREV, REQUEST_SCENARIO3_NAGREV, SCENARIO1_NAGREV, SCENARIO2_NAGREV, SCENARIO3_NAGREV
    {
      case REQUEST_RESET_NAGREV:
        nagrevMode = NAGREV_OFF;
        break;
      case REQUEST_SCENARIO1_NAGREV:
        nagrevMode = SCENARIO1_NAGREV;
        break;
      case REQUEST_SCENARIO2_NAGREV:
        nagrevMode = SCENARIO2_NAGREV;
        break;
      case REQUEST_SCENARIO3_NAGREV:
        nagrevMode = SCENARIO3_NAGREV;
        break;
    }
    dtmfCommandMode = DC_EXECUTED;
  }

  if ((ventMode == REQUEST_RESET_VENT ||
       ventMode == REQUEST_SCENARIO1_VENT ||
       ventMode == REQUEST_SCENARIO2_VENT ||
       ventMode == REQUEST_SCENARIO3_VENT) && dtmfCommandMode == DC_CONFIRMED || nrfCommandProcessing)

  {
    SetVentScenario();
    switch (ventMode) //VENT_OFF, REQUEST_RESET_VENT, REQUEST_SCENARIO1_VENT, REQUEST_SCENARIO2_VENT, REQUEST_SCENARIO3_VENT, SCENARIO1_VENT, SCENARIO2_VENT, SCENARIO3_VENT
    {
      case REQUEST_RESET_VENT:
        ventMode = VENT_OFF;
        break;
      case REQUEST_SCENARIO1_VENT:
        ventMode = SCENARIO1_VENT;
        break;
      case REQUEST_SCENARIO2_VENT:
        ventMode = SCENARIO2_VENT;
        break;
      case REQUEST_SCENARIO3_VENT:
        ventMode = SCENARIO3_VENT;
        break;
    }
    dtmfCommandMode = DC_EXECUTED;
  }


  //  switch (addDeviceMode) //ADD_DEVICE_OFF, REQUEST_ADD_DEVICE_ON, ADD_DEVICE_ON, REQUEST_ADD_DEVICE_OFF
  //  {
  //  case REQUEST_ADD_DEVICE_ON:
  //    if (dtmfCommandMode == DC_CONFIRMED)
  //    {
  //      onPeriodAddDevice_ms = 0;
  //      digitalWrite(ADD_DEVICE_PIN, HIGH);
  //      addDeviceMode = ADD_DEVICE_ON;
  //      dtmfCommandMode = DC_EXECUTED;
  //    }
  //    break;
  //  case REQUEST_ADD_DEVICE_OFF:
  //    if (dtmfCommandMode == DC_CONFIRMED)
  //    {
  //      digitalWrite(ADD_DEVICE_PIN, LOW);
  //      addDeviceMode = ADD_DEVICE_OFF;
  //      if (addDTMFParam == 0)
  //        onPeriodAddDevice_s = MAX_ON_PERIOD_ADD_DEVICE_S;
  //      else
  //        onPeriodAddDevice_s = addDTMFParam;
  //      dtmfCommandMode = DC_EXECUTED;
  //    }
  //    break;
  //  case ADD_DEVICE_ON:
  //    if (onPeriodAddDevice_ms > onPeriodAddDevice_s * 1000)
  //    {
  //      digitalWrite(ADD_DEVICE_PIN, LOW);
  //      addDeviceMode = ADD_DEVICE_OFF;
  //      break;
  //    }
  //  }

  //  switch (registratorMode) //REGISTRATOR_OFF, REQUEST_REGISTRATOR_ON, REGISTRATOR_ON
  //  {
  //  case REQUEST_REGISTRATOR_ON:
  //    if (dtmfCommandMode == DC_CONFIRMED)
  //    {
  //      onPeriodRegistrator_ms = 0;
  //      digitalWrite(REGISTRATOR_PIN, HIGH);
  //      registratorMode = REGISTRATOR_ON;
  //      dtmfCommandMode = DC_EXECUTED;
  //    }
  //    break;
  //  case REGISTRATOR_ON:
  //    if (onPeriodRegistrator_ms > ON_PERIOD_REGISTRATOR * 1000)
  //    {
  //      digitalWrite(REGISTRATOR_PIN, LOW);
  //      registratorMode = REGISTRATOR_OFF;
  //      break;
  //    }
  //  }

  //MIC_OFF, REQUEST_MIC_ON, MIC_ON
  if (micMode == REQUEST_MIC_ON && dtmfCommandMode == DC_CONFIRMED)
  {
    digitalWrite(MIC_PIN, HIGH);
    micMode = MIC_ON;
    dtmfCommandMode = DC_EXECUTED;
  }
  else if (micMode == MIC_ON && gsmMode == WAIT_GSM)
  {
    digitalWrite(MIC_PIN, LOW);
    micMode = MIC_OFF;
  }
  nrfCommandProcessing = false;
}

//for incoming and outgoing calls
void BreakCall()
{
  mySerialGSM.println("AT");
  _delay_ms(200);

  mySerialGSM.println("ATH");
  _delay_ms(500);
}

void AnswerCall()
{
  mySerialGSM.println("AT");
  _delay_ms(200);
  mySerialGSM.println("ATA");  //ответ
  _delay_ms(200);
}

//Reset all statuses in the end
void FinalResetStatuses()
{
  Serial.println("FinalReset");
  if (addDeviceMode == REQUEST_ADD_DEVICE_ON)
    addDeviceMode = ADD_DEVICE_OFF;
  if (addDeviceMode == REQUEST_ADD_DEVICE_OFF)
    addDeviceMode = ADD_DEVICE_ON;
  if (registratorMode == REQUEST_REGISTRATOR_ON)
    registratorMode = REGISTRATOR_OFF;
  micMode = MIC_OFF;
  sendSMSMode = SMS_NO;
  addDTMFParam = 0;

  dtmfCommandMode = DC_WAIT;
  gsmMode = WAIT_GSM;
  mySerialGSM.println("AT");
  _delay_ms(100);
  mySerialGSM.println("AT");
}


void ReadCommandNRF()
{
  if (radio.available())
  {
    Serial.println("radio.available!!");
    digitalWrite(BZZ_PIN, HIGH);
    _delay_ms(500);
    digitalWrite(BZZ_PIN, LOW);
    while (radio.available()) // While there is data ready
    {
      radio.read(&inNRFCommand, sizeof(inNRFCommand)); // по адресу переменной inNRFCommand функция записывает принятые данные
      _delay_ms(20);
      Serial.println("radio.available: ");
      //Serial.println(inNRFCommand);
    }
    ParseAndHandleInputNrfCommand();
    radio.startListening();                                // Now, resume listening so we catch the next packets.
    DisplayData(1);
  }
}

//send Tout
void SendCommandNRF(byte roomNumber)
{
  outNRFCommand.Command = OUT_T_INFO;
  outNRFCommand.roomNumber = roomNumber;
  outNRFCommand.tOut = t_out;
  outNRFCommand.p_v = p_v;
  outNRFCommand.nagrevStatus = nagrevStatus[roomNumber];

  Serial.print("SendCommandNRF: ");
  // Serial.println(outNRFCommand);
  radio.startListening();
  radio.stopListening();

  if (radio.write(&outNRFCommand, sizeof(outNRFCommand)))
  {
    Serial.println("Success Send");
    //lastSend_ms = 0;
  }
  else
  {
    Serial.println("Failed Send");
  }
  radio.startListening();
}

void ParseAndHandleInputNrfCommand()
{
  nrfCommandProcessing = true;
  switch (inNRFCommand.Command) //IN_NO, IN_ROOM_INFO, IN_ROOM_COMMAND, IN_CENTRAL_COMMAND
  {
    case IN_ROOM_INFO:
      t_inn[inNRFCommand.roomNumber] = inNRFCommand.t;
      co2[inNRFCommand.roomNumber] = inNRFCommand.co2;
      h[inNRFCommand.roomNumber] = inNRFCommand.h;
      t_set[inNRFCommand.roomNumber] = inNRFCommand.t_set;

      switch (inNRFCommand.alarmType) // { ALR_NO, ALR_VODA, ALR_DOOR }
      {
        case ALR_VODA:
          InformCall(inNRFCommand.roomNumber == ROOM_VANNA1 ? CI_VODA1 : CI_VODA2);
          break;
        case ALR_DOOR:
          InformCall(inNRFCommand.roomNumber == ROOM_HALL ? CI_ALARM1 : CI_ALARM2);
          break;
      }

    case IN_ROOM_COMMAND:
      //V_TO_AUTO, V_AUTO_SPEED1, V_AUTO_SPEED2, V_AUTO_OFF, V_TO_SPEED1, V_TO_SPEED2, V_SPEED1, V_SPEED2, V_TO_OFF, V_OFF
      switch (inNRFCommand.ventSpeed) //0-not supported, 1-1st speed, 2-2nd speed, 10 - off, 100 - auto
      {
        case 10: //off
          if (modeVent[inNRFCommand.roomNumber] != V_OFF)
            modeVent[inNRFCommand.roomNumber] = V_TO_OFF;
          break;
        case 1: //1st speed
          if (modeVent[inNRFCommand.roomNumber] != V_SPEED1)
            modeVent[inNRFCommand.roomNumber] = V_TO_SPEED1;
          break;
        case 2: //2d speed
          if (modeVent[inNRFCommand.roomNumber] != V_SPEED2)
            modeVent[inNRFCommand.roomNumber] = V_TO_SPEED2;
          break;
        case 100: //auto
          modeVent[inNRFCommand.roomNumber] = V_TO_AUTO;
          break;
      }
      break;

    case IN_CENTRAL_COMMAND:
      switch (inNRFCommand.scenarioVent) //0-reset, 1, 2, 3
      {
        case 0: //off
          ventMode = REQUEST_RESET_VENT;
          break;
        case 1: //1st scenario
          ventMode = REQUEST_SCENARIO1_VENT;
          break;
        case 2:
          ventMode = REQUEST_SCENARIO2_VENT;
          break;
        case 3:
          ventMode = REQUEST_SCENARIO3_VENT;
          break;
      }
      switch (inNRFCommand.scenarioNagrev) //0-reset, 1, 2, 3
      {
        case 0: //off
          nagrevMode = REQUEST_RESET_NAGREV;
          break;
        case 1: //1st scenario
          nagrevMode = REQUEST_SCENARIO1_NAGREV;
          break;
        case 2:
          nagrevMode = REQUEST_SCENARIO2_NAGREV;
          break;
        case 3:
          nagrevMode = REQUEST_SCENARIO3_NAGREV;
          break;
      }
      break;
  }
}

void RefreshSensorData()
{
  if (lastRefreshSensor_ms > REFRESH_SENSOR_INTERVAL_S * 1000)
  {
    sensors.requestTemperatures();
    //float realTemper = sensors.getTempCByIndex(0);
    //t_inn = sensors.getTempC(innerTempDeviceAddress);
    t_out1 = sensors.getTempC(outer1TempDeviceAddress);
    t_out2 = sensors.getTempC(outer2TempDeviceAddress);
    t_vent = sensors.getTempC(ventTempDeviceAddress);
    t_unit = sensors.getTempC(unitTempDeviceAddress);

    t_out = t_out1 < t_out2 ? t_out1 : t_out1;
    h[ROOM_GOST] = dht.readHumidity();
    t_inn[ROOM_GOST] = dht.readTemperature();
    p_v = 0.0075 * bmp.readPressure();

    SendCommandNRF(0); //send to all (roomNumber=0)

    lastRefreshSensor_ms = 0;
  }
}

// V_TO_AUTO, V_AUTO_SPEED1, V_AUTO_SPEED2, V_AUTO_OFF, V_TO_SPEED1, V_TO_SPEED2, V_SPEED1, V_SPEED2, V_TO_OFF, V_OFF
void VentControl()
{
  //for (byte i = 0; i < 5; i++)
  switch (modeVent[ROOM_BED])
  {
    case V_TO_AUTO:
    case V_AUTO_SPEED1:
    case V_AUTO_SPEED2:
    case V_AUTO_OFF:
      if (ventCorrectionPeriod_ms > VENT_CORRECTION_PERIOD_S * 1000)
      {
        if (co2[1] > PPM_SWITCH_ON_MAX_VENT)
          modeVent[ROOM_BED] = V_AUTO_SPEED2;
        else if (co2[1] > PPM_SWITCH_ON_VENT)
          modeVent[ROOM_BED] = V_AUTO_SPEED1;
        else if (co2[1] <= PPM_SWITCH_OFF_VENT)
          modeVent[ROOM_BED] = V_AUTO_OFF;
        break;

        //reduce speed or off if too cold in room
        if (t_inn[1] < (t_out < BORDER_WINTER_SUMMER ? MIN_COMFORT_ROOM_TEMP_WINTER : MIN_COMFORT_ROOM_TEMP_SUMMER) && t_inn[1] > t_vent) //t_inn too cold
        {
          modeVent[ROOM_BED] = (modeVent[ROOM_BED] == V_AUTO_SPEED2 ? V_AUTO_SPEED1 : V_AUTO_OFF);
        }

        ventCorrectionPeriod_ms = 0;
      }

    case V_TO_OFF:
      modeVent[ROOM_BED] = V_OFF;
      break;
    case V_TO_SPEED1:
      modeVent[ROOM_BED] = V_SPEED1;
      break;
    case V_TO_SPEED2:
      modeVent[ROOM_BED] = V_SPEED2;
      break;
  }

  digitalWrite(VENT_SPEED1_PIN, (modeVent[ROOM_BED] == V_SPEED1 || modeVent[ROOM_BED] == V_AUTO_SPEED1));
  digitalWrite(VENT_SPEED2_PIN, (modeVent[ROOM_BED] == V_SPEED2 || modeVent[ROOM_BED] == V_AUTO_SPEED2));
}

void NagrevControl()
{
  if (nagrevControlPeriod_ms > NAGREV_CONTROL_PERIOD_S * 1000)
  {
    for (byte i = 0; i < 5; i++)
    {
      boolean prevStatus = nagrevStatus[i];
      nagrevStatus[i] = (t_inn[i] < t_set[i]);
      //digitalWrite(VENT_SPEED1_PIN, (modeVent == V_SPEED1 || modeVent == V_AUTO_SPEED1));
      if (prevStatus != nagrevStatus[i]) //if changed, send command immediatly (and will repeat every minute together with sending T_out info)
      {
        SendCommandNRF(i + 1);
      }
    }
    nagrevControlPeriod_ms = 0;
  }
}

void Check220()
{
  if (!digitalRead(U_220_PIN))
  {
    if (!bNo220)  //ловим момент пропадания
    {
      No220Actions();
    }
    bNo220 = true;
  }
  else
    bNo220 = false;
}

void No220Actions()
{
  InformCall(CI_NO_220);
}

void loop()
{
  GetSoftSerialData();
  CheckSoftwareSerialData();
  WorkflowGSM();
  WorkflowMain(0);

  RefreshSensorData();
  CheckAlarmLine();

  ReadCommandNRF();

  VentControl();
  //NagrevControl();
  Check220();
  DisplayData(0);

  wdt_reset();
}
