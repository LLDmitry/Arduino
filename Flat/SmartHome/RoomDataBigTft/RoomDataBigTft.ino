//Room data modified (get T_out and p_v, send CO2)
//Аппаратный SPI  http://www.poprobot.ru/home/Arduino-TFT-SPI  http://robotchip.ru/podklyuchenie-tft-displeya-1-8-k-arduino/
//Как известно, Arduino имеет встроенный аппаратный SPI. На Arduino Nano для этого используются выводы с 10 по 13.
// move nrf to soft spi http://forum.amperka.ru/threads/nrf24l-spi-tft-display.7748/   https://forum.arduino.cc/index.php?topic=545575.0
// http://forum.amperka.ru/threads/nrf24l01-%D0%BF%D0%BE%D0%B1%D0%B5%D0%B6%D0%B4%D0%B0%D0%B5%D0%BC-%D0%BC%D0%BE%D0%B4%D1%83%D0%BB%D1%8C.3205/page-36
//TFT: https://www.arduino.cc/en/Guide/TFT

#include <NrfCommands.h>
#include "SoftwareSerial.h"
#include "DHT.h"
#include <elapsedMillis.h>
#include <Wire.h>
#include <EEPROM.h>
#include "nRF24L01.h"
#include <RF24.h>
#include <RF24_config.h>
#include <stdio.h> // for function sprintf
#include <IRremote.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <IRremote.h>

#include "Adafruit_GFX.h"     // Библиотека обработчика графики
#include "Adafruit_ILI9341.h" // Программные драйвера для дисплеев ILI9341

#define DHTTYPE DHT22

#define CO2_TX        A0
#define CO2_RX        A1

//RNF  SPI bus plus pins 9 & 10  9,10 для Уно или 9, 53 для Меги
#define RNF_CE_PIN    8
#define RNF_CSN_PIN   10
#define RNF_MOSI      11
#define RNF_MISO      12
#define RNF_SCK       13

#define TFT_CS        2            // Указываем пины cs
#define TFT_RST       3            // Указываем пины reset
#define TFT_DC        4            // Указываем пины dc (A0)
#define TFT_MOSI      A5            // Пин подключения вывода дисплея SDI(MOSI) SDA
#define TFT_CLK       A3            // Пин подключения вывода дисплея SCK SCL
#define TFT_MISO      -1           // Пин подключения вывода дисплея SDO(MISO)s

#define BZZ_PIN       9
#define DHT_PIN       7
#define BTTN_PIN      -1 //5
#define IR_RECV_PIN   -1 //6
#define LGHT_SENSOR_PIN    A4
#define LED_PIN       A2

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)

DHT dht(DHT_PIN, DHTTYPE);

const byte ROOM_NUMBER = ROOM_GOST;

/* some RGB color definitions                                                 */
#define BLUE           0x001F
#define BLACK           0x0000      /*   0,   0,   0 */
#define NAVY            0x000F      /*   0,   0, 128 */
#define DARK_GREEN      0x03E0      /*   0, 128,   0 */
#define DARK_CYAN       0x03EF      /*   0, 128, 128 */
#define MAROON          0x7800      /* 128,   0,   0 */
#define PURPLE          0x780F      /* 128,   0, 128 */
#define OLIVE           0x7BE0      /* 128, 128,   0 */
#define LIGHT_GREAY     0xC618      /* 192, 192, 192 */
#define DARK_GREAY      0x7BEF      /* 128, 128, 128 */
#define GREEN           0x07E0      /*   0, 255,   0 */
#define CYAN            0x07FF      /*   0, 255, 255 */
#define RED             0xF800      /* 255,   0,   0 */
#define MAGENTA         0xF81F      /* 255,   0, 255 */
#define YELLOW          0xFFE0      /* 255, 255,   0 */
#define WHITE           0xFFFF      /* 255, 255, 255 */
#define ORANGE          0xFD20      /* 255, 165,   0 */
#define LEMON           0xAFE5      /* 173, 255,  47 */
#define PINK            0xF81F




const uint32_t REFRESH_SENSOR_INTERVAL_S = 60;  //1 мин
const uint32_t SAVE_STATISTIC_INTERVAL_S = 1800; //30мин
const uint32_t CHANGE_STATISTIC_INTERVAL_S = 5;
const uint32_t SET_LED_INTERVAL_S = 5;
const uint32_t READ_COMMAND_NRF_INTERVAL_S = 1;

const byte NUMBER_STATISTICS = 60;

const byte BASE_VAL_T_IN = 190;
const byte BASE_VAL_T_OUT = 0;  //-25
const byte BASE_VAL_HM_IN = 10;
const byte BASE_VAL_CO2 = 40;
const byte BASE_VAL_P = 210;
const byte TOP_VAL_T_IN = 300;
const byte TOP_VAL_T_OUT = 250; //+25
const byte TOP_VAL_HM_IN = 80;
const byte TOP_VAL_CO2 = 150;
const byte TOP_VAL_P = 260;

const byte DIFF_VAL_T_IN = 2;
const byte DIFF_VAL_T_OUT = 2;
const byte DIFF_VAL_HM_IN = 2;
const byte DIFF_VAL_CO2 = 5;
const byte DIFF_VAL_P = 2;

const int LEVEL1_CO2_ALARM = 600;
const int LEVEL2_CO2_ALARM = 900;
const int LEVEL3_CO2_ALARM = 1200;

const int LIGHT_LEVEL_DARK = 370;

const int EEPROM_ADR_INDEX_STATISTIC = 1023; //last address in eeprom for store indexStatistic
const uint16_t HEIGHT_DISPLAY = 236;

// Single radio pipe address for the 2 nodes to communicate.  Значение "трубы" передатчика и приемника ОБЯЗАНЫ быть одинаковыми.
//const uint64_t readingPipe = 0xE8E8F0F0AALL;  // д.б. свой для каждого блока
//const uint64_t writingPipe = 0xE8E8F0F0ABLL;  // д.б. один для всех

const String IR_CODE_VENT1 = "38863bc2";
const String IR_CODE_VENT2 = "38863bca";
const String IR_CODE_VENT_STOP = "38863bca";
const String IR_CODE_VENT_AUTO = "38863bca";

const byte W1 = 135;
const byte W2 = 95;
const byte W3 = 90;
const byte H1 = 50;
const byte H2 = 50;

elapsedMillis refreshSensors_ms = REFRESH_SENSOR_INTERVAL_S * 1000 + 1;
elapsedMillis saveStatistic_ms = (SAVE_STATISTIC_INTERVAL_S - REFRESH_SENSOR_INTERVAL_S * 3) * 1000;
elapsedMillis changeStatistic_ms = 0;
elapsedMillis setLed_ms = 0;
elapsedMillis readCommandNRF_ms = 0;

IRrecv irrecv(IR_RECV_PIN);

char* parameters[] = {"Temper In", "Temper Out", "Humidity", "CO2", "Pressure"};

byte h_v = 0;
float t_inn = 0.0f;
int ppm_v = 0;

int prev_tOut_int = 99;
int prev_tOut_dec = 99;
float prev_t_inn = 0.0f;
byte prev_h_v = 0;
int prev_ppm_v = 0;
int prev_p_v = 0;
byte prev_hours = 0;
byte prev_minutes = 0;
unsigned int prev_co2backColor;

byte Mode = 0;
byte prevGraph[NUMBER_STATISTICS];
byte values[NUMBER_STATISTICS];
byte indexStatistic = 0;

String alertedRooms = "";

NRFResponse nrfResponse;
NRFRequest nrfRequest;

SoftwareSerial mySerial(CO2_TX, CO2_RX); // TX, RX сенсора

// Set up nRF24L01 radio on SPI bus plus pins 9 & 10  9,10 для Уно или 9, 53 для Меги
RF24 radio(RNF_CE_PIN, RNF_CSN_PIN);

Adafruit_ILI9341 TFTscreen = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);  // Создаем объект дисплея и сообщаем библиотеке распиновку для работы с графикой

void setup()
{
  Serial.begin(9600);

  mySerial.begin(9600);

  dht.begin();

  EEPROM.get(EEPROM_ADR_INDEX_STATISTIC, indexStatistic);
  //indexStatistic = 5;

  // RF24
  radio.begin();                          // Включение модуля;
  _delay_ms(10);
  radio.enableAckPayload();                     // Allow optional ack payloads
  //radio.enableDynamicPayloads();                // Ack payloads are dynamic payloads

  radio.setPayloadSize(32); //18
  radio.setChannel(ArRoomsChannelsNRF[ROOM_NUMBER]);
  radio.setRetries(0, 10);                // Установка интервала и количества попыток "дозвона" до приемника;
  radio.setDataRate(RF24_1MBPS);        // Установка скорости(RF24_250KBPS, RF24_1MBPS или RF24_2MBPS), RF24_250KBPS на nRF24L01 (без +) неработает.
  radio.setPALevel(RF24_PA_MAX);          // Установка максимальной мощности;
  //radio.setAutoAck(0);                    // Установка режима подтверждения приема;
  radio.openWritingPipe(CentralReadingPipe);     // Активация данных для отправки
  radio.openReadingPipe(1, RoomReadingPipe);   // Активация данных для чтения
  radio.startListening();

  radio.printDetails();

  irrecv.enableIRIn(); // Start the ir receiver

  pinMode(BZZ_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTTN_PIN, INPUT);

  Serial.print("ROOM_NUMBER=");
  Serial.println(ROOM_NUMBER);
  _delay_ms(10);


  TFTscreen.width();
  TFTscreen.height();
  TFTscreen.begin();
  TFTscreen.setRotation(3);  // 0,2 - Portrait, 1,3 - Lanscape

  /*
    Установка цвета фона TFTscreen.background ( r , g , b )
    где, r, g и b являются значениями RGB для заданного цвета
  */
  //TFTscreen.background(0, 0, 0);
  /*
    Команда установки цвета фонта TFTscreen.stroke ( r , g , b )
    где, r, g и b являются значениями RGB для заданного цвета
  */
  TFTscreen.setTextSize(3);      // Устанавливаем размер шрифта
  DrawRect(0, 0, 320, 240, BLACK, true);
  //DrawRect(0, 0, W1 - 1, H1 - 1, DARK_GREAY, true); //время

  wdt_enable(WDTO_8S);
}

void(* resetFunc) (void) = 0; // объявляем функцию reset с адресом 0

//void AutoChangeShowMode()
//{
//  if (showMode_ms > SHOW_MODE_INTERVAL_S * 1000)
//  {
//    //    //Serial.print("showMode_ms ");
//    //    //Serial.println(millis());
//    //    Mode += 1;
//    //    if (Mode > 5) Mode = 1;
//    //
//    showMode_ms = 0;
//    Mode = 1; //!!!test
//
//  }
//}

void RefreshSensorData()
{
  byte cmd[9] = { 0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79 };
  //char response[9];
  unsigned char response[9];

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  if (refreshSensors_ms > REFRESH_SENSOR_INTERVAL_S * 1000)
  {
    Serial.println("RefreshSensorData");
    //Serial.print("startrefreshSensors_ms ");
    //Serial.println(millis());
    h_v = dht.readHumidity();
    //Serial.println("readHumidity ");
    t_inn = dht.readTemperature();
    t_inn = t_inn - 1.5;
    //Serial.println("readTemperature ");

    mySerial.write(cmd, 9);
    memset(response, 0, 9);
    mySerial.readBytes(response, 9);

    int i;
    byte crc = 0;
    for (i = 1; i < 8; i++) crc += response[i];
    crc = 255 - crc;
    crc++;

    if ( !(response[0] == 0xFF && response[1] == 0x86 && response[8] == crc) )
    {
      Serial.println("CRC error: " + String(crc) + " / " + String(response[8]));
    } else
    {
      unsigned int responseHigh = (unsigned int) response[2];
      unsigned int responseLow = (unsigned int) response[3];
      ppm_v = (256 * responseHigh) + responseLow;
      if (ppm_v < 400) ppm_v = 400;
      Serial.print("co2= ");
      Serial.println(ppm_v);
    }

    if (ppm_v == 0)
    {
      Serial.println("RESET in 3 sec");
      _delay_ms(3000);
      resetFunc(); //вызов reset
    }

    PrepareCommandNRF(RSP_INFO, 100, -100, 99, 99);

    refreshSensors_ms = 0;

    DisplayData();

    if (saveStatistic_ms > SAVE_STATISTIC_INTERVAL_S * 1000)
    {
      SaveStatistic();

      Serial.print("indexStatistic2 ");
      Serial.println(indexStatistic);

      //ShowStatistic();
      saveStatistic_ms = 0;
    }
  }
}

void ChangeStatistic()
{
  if (changeStatistic_ms > CHANGE_STATISTIC_INTERVAL_S * 1000)
  {
    Mode++;
    if (Mode == 3) Mode = 4;
    if (Mode > 5) Mode = 1;
    Serial.print("indexStatisticA ");
    Serial.println(indexStatistic);
    ShowStatistic();
    Serial.print("indexStatisticB ");
    Serial.println(indexStatistic);
    changeStatistic_ms = 0;
  }
}


//BLACK           0x0000      /*   0,   0,   0 */
//#define NAVY            0x000F      /*   0,   0, 128 */
//#define DARK_GREEN      0x03E0      /*   0, 128,   0 */
//#define DARK_CYAN       0x03EF      /*   0, 128, 128 */
//#define MAROON          0x7800      /* 128,   0,   0 */
//#define PURPLE          0x780F      /* 128,   0, 128 */
//#define OLIVE           0x7BE0      /* 128, 128,   0 */
//#define LIGHT_GREAY     0xC618      /* 192, 192, 192 */
//#define DARK_GREAY      0x7BEF      /* 128, 128, 128 */
//#define BLUE            0x001F      /*   0,   0, 255 */
//#define GREEN           0x07E0      /*   0, 255,   0 */
//#define CYAN            0x07FF      /*   0, 255, 255 */
//#define RED             0xF800      /* 255,   0,   0 */
//#define MAGENTA         0xF81F      /* 255,   0, 255 */
//#define YELLOW          0xFFE0      /* 255, 255,   0 */
//#define WHITE           0xFFFF      /* 255, 255, 255 */
//#define ORANGE          0xFD20      /* 255, 165,   0 */
//#define LEMON           0xAFE5      /* 173, 255,  47 */
//#define PINK
void DisplayData()
{
  Serial.println("DisplayData");
  char printout[128];
  char str_temp[5];
  int tOut_int = (int)nrfRequest.tOut;
  int tOut_dec = (float)((float)nrfRequest.tOut - (float)tOut_int) * 10.0;

  Serial.println(prev_tOut_dec);
  Serial.println(tOut_dec);
  if (tOut_dec != prev_tOut_dec || tOut_int != prev_tOut_int)
  {
    if (tOut_int != prev_tOut_int)
    {
      TFTscreen.setTextColor(BLACK);
      TFTscreen.setTextSize(5);
      PrintText(String(abs(prev_tOut_int)), abs(prev_tOut_int) < 10 ? 40 : 15, 30);
      PrintText("." + String(abs(prev_tOut_dec)), abs(prev_tOut_int) < 10 ? 72 : 80, 45);

      TFTscreen.setTextColor(CYAN);
      PrintText(String(abs(tOut_int)), abs(nrfRequest.tOut) < 10.0 ? 40 : 15, 30);
    }

    if ((prev_tOut_int < 0 || prev_tOut_dec < 0) && (!(tOut_int < 0 || tOut_dec < 0) || tOut_int != prev_tOut_int))
    {
      TFTscreen.setTextColor(BLACK);
      TFTscreen.setTextSize(2);
      PrintText("-", abs(prev_tOut_int) < 10 ? 22 : 2, 41);
    }
    TFTscreen.setTextColor(CYAN);
    if (tOut_int < 0 || tOut_dec < 0)
    {
      TFTscreen.setTextSize(2);      // Устанавливаем размер шрифта
      PrintText("-", abs(tOut_int) < 10.0 ? 22 : 2, 41);
    }

    if (tOut_dec != prev_tOut_dec || tOut_int != prev_tOut_int)
    {
      TFTscreen.setTextColor(BLACK);
      TFTscreen.setTextSize(3);
      PrintText("." + String(abs(prev_tOut_dec)), abs(prev_tOut_int) < 10 ? 72 : 80, 45);

      TFTscreen.setTextColor(CYAN);
      PrintText("." + String(abs(tOut_dec)), abs(tOut_int) < 10 ? 72 : 80, 45);
    }

    prev_tOut_int = tOut_int;
    prev_tOut_dec = tOut_dec;
    TFTscreen.setTextSize(3);
  }

  if (nrfRequest.p_v != prev_p_v)
  {
    TFTscreen.setTextColor(BLACK);
    sprintf(printout, "%d", prev_p_v);
    PrintText(printout, W1 + 20, H1 + 13);
    TFTscreen.setTextColor(CYAN);
    sprintf(printout, "%d", nrfRequest.p_v / 10);
    PrintText(printout, W1 + 20, H1 + 13);
    prev_p_v = nrfRequest.p_v / 10;
  }

  if (t_inn != prev_t_inn)
  {
    TFTscreen.setTextColor(BLACK);
    dtostrf(prev_t_inn, 4, 1, str_temp);
    sprintf(printout, "%s", str_temp);
    PrintText(printout, W1 + (prev_t_inn < 10 ? 20 : 12), 14);
    TFTscreen.setTextColor(CYAN);
    dtostrf(t_inn, 4, 1, str_temp);
    sprintf(printout, "%s", str_temp);
    PrintText(printout, W1 + (t_inn < 10 ? 20 : 12), 14);
    prev_t_inn = t_inn;
  }

  if (ppm_v != prev_ppm_v)
  {
    unsigned int co2backColor;
    if (ppm_v < LEVEL1_CO2_ALARM)
    {
      //co2backColor = 0x022F;
      co2backColor = BLUE;
    }
    else if (ppm_v < LEVEL2_CO2_ALARM)
    {
      co2backColor = OLIVE;
    }
    else if (ppm_v < LEVEL3_CO2_ALARM)
    {
      co2backColor = MAGENTA;
    }
    else
    {
      co2backColor = RED;
    }

    if (prev_co2backColor != co2backColor)
    {
      DrawRect(W1 + W2 - 1, 1, W3, H1 - 2, co2backColor, true); //CO2
    }
    else
    {
      TFTscreen.setTextColor(co2backColor);
      PrintText(String(prev_ppm_v), prev_ppm_v < 1000 ? W1 + W2 + 20 : W1 + W2 + 10, 15);
    }

    TFTscreen.setTextColor(CYAN);
    PrintText(String(ppm_v), ppm_v < 1000 ? W1 + W2 + 20 : W1 + W2 + 10, 15);
    prev_ppm_v = ppm_v;
    prev_co2backColor = co2backColor;
  }

  if (h_v != prev_h_v)
  {
    TFTscreen.setTextColor(BLACK);
    sprintf(printout, "%d", prev_h_v);
    PrintText(printout, W1 + W2 + 30, H1 + 13);
    TFTscreen.setTextColor(CYAN);
    sprintf(printout, "%d", h_v);
    PrintText(printout, W1 + W2 + 30, H1 + 13);
    prev_h_v = h_v;
  }
}

void ShowStatistic()
{
  byte val = 0;
  byte minVal = 255;
  byte maxVal = 0;
  byte i;
  byte iprev;
  byte baseVal;
  byte topVal;
  byte val_prev;
  byte val_last;
  unsigned int colorLine;
  const byte HEIGHT_GRAPH = 130;

  DrawRect(0, 0, W1, H1 + H2 - 1, DARK_GREAY, false); //T out
  DrawRect(W1 + W2 - 2, 0, W3 + 1, H1, DARK_GREAY, false); //CO2
  DrawRect(W1 - 1, H1  - 1, W2, H2, DARK_GREAY, false); //P
  DrawRect(W1 - 1, 0, W2, H1, DARK_GREAY, false); //T in
  DrawRect(W1 + W2 - 2, H1 - 1, W3 + 1, H2, DARK_GREAY, false); //Hm

  //hide prev graph
  HidePrevGraph();

  switch (Mode)
  {
    case 1: //T inn
      DrawRect(W1 - 1, 0, W2, H1, WHITE, false); //T in
      break;
    case 2: //T out
      DrawRect(0, 0, W1, H1 + H2 - 1, WHITE, false); //T out
      break;
    case 4: //CO2
      DrawRect(W1 + W2 - 2, 0, W3 + 1, H1, WHITE, false); //CO2
      break;
    case 5: //P
      DrawRect(W1 - 1, H1 - 1, W2, H2, WHITE, false); //P
      break;
  }

  ReadFromEEPROM(Mode);  //into values[]

  minVal = 255;
  maxVal = 0;

  for (byte i = 0; i < NUMBER_STATISTICS; i++)
  {
    val = values[i];

    if (val < minVal)
    {
      minVal = val;
    }
    if (val > maxVal)
    {
      maxVal = val;
    }
  }

  switch (Mode)
  {
    case 1: //T inn
      topVal = maxVal > 250 ? 255 : maxVal + 5;
      baseVal = minVal > topVal - 30 ? (topVal < 30 ? 0 : topVal - 30) : minVal; //30 = 3c
      colorLine = RED;
      break;
    case 2: //T out
      topVal = maxVal > 251 ? 255 : maxVal + 4;
      baseVal = minVal < 4 ? 0 : minVal - 4;

//      if ((topVal - baseVal) < 50)
//      {
//        int avrgVal = (topVal + baseVal) / 2;
//        if ((avrgVal + 25) > 251)
//        {
//          topVal = 252;
//          baseVal =  topVal - 50;
//        }
//        else if (avrgVal <= 25)
//        {
//          baseVal = 0;
//          topVal = baseVal + 50;
//        }
//        else
//        {
//          topVal = avrgVal + 25;
//          baseVal = avrgVal - 25;
//        }
//      }
      colorLine = GREEN;
      break;
    case 3: //Влажн
      baseVal = BASE_VAL_HM_IN;
      topVal = TOP_VAL_HM_IN;
      colorLine = MAGENTA;
      break;
    case 4: //CO2
      baseVal = BASE_VAL_CO2;
      topVal = TOP_VAL_CO2;
      colorLine = CYAN;
      break;
    case 5: //P
      topVal = maxVal > 252 ? 255 : maxVal + 3;
      baseVal = minVal < 3 ? 0 : minVal - 3;
      if ((topVal - baseVal) < 50) //<17mm
      {
        int avrgVal = (topVal + baseVal) / 2;
        if ((avrgVal + 25) > 251)
        {
          topVal = 252;
          baseVal =  topVal - 50;
        }
        else if (avrgVal <= 25)
        {
          baseVal = 0;
          topVal = baseVal + 50;
        }
        else
        {
          topVal = avrgVal + 25;
          baseVal = avrgVal - 25;
        }
        colorLine = YELLOW;
      }
      else
      {
        baseVal = 0;
        topVal = 255;
        colorLine = ORANGE;
      }
      break;
  }

  float k = (float)HEIGHT_GRAPH / (float)(topVal - baseVal);

  for (byte x = 0; x < NUMBER_STATISTICS; x++)
  {
    i = indexStatistic + x;
    if (i >= NUMBER_STATISTICS)
    {
      i = i - NUMBER_STATISTICS;
    }

    if (x == NUMBER_STATISTICS - 2)
    {
      val_prev = values[i];
    }
    if (x == NUMBER_STATISTICS - 1)

    {
      val_last = values[i];
    }
    if (x > 0)
    {
      byte val1 = values[iprev];
      byte val2 = values[i];
      int y1 = (int)(k * (val1 - baseVal));

      int y2 = (int)(k * (val2 - baseVal));
      if (y1 > (HEIGHT_GRAPH - 2)) y1 = HEIGHT_GRAPH - 2;
      if (y1 < 2) y1 = 2;
      if (y2 > (HEIGHT_GRAPH - 2)) y2 = HEIGHT_GRAPH - 2;
      if (y2 < 2) y2 = 2;
      int x1 = 10 + (x - 1) * 5;
      int x2 = 10 + x * 5;
      TFTscreen.drawLine(x1, (HEIGHT_DISPLAY - (byte)y1), x2, (HEIGHT_DISPLAY - (byte)y2), colorLine);

      if (x == 1) prevGraph[0] = (byte)y1;
      prevGraph[x] = (byte)y2;
    }
    iprev = i;
  }
  //ShowChangeMark(val_last, val_prev);
}


void HidePrevGraph()
{
  for (byte x = 1; x <= NUMBER_STATISTICS - 1; x++)
  {
    int x1 = 10 + (x - 1) * 5;
    int x2 = 10 + x * 5;
    TFTscreen.drawLine(x1, (HEIGHT_DISPLAY - prevGraph[x - 1]), x2, (HEIGHT_DISPLAY - prevGraph[x]), BLACK);
  }
}

void SaveStatistic()
{
  Serial.println("EEPROM.put(indexStatistic, ConvertToByte(4, ppm_v);");
  Serial.println(ppm_v);
  Serial.println(ConvertToByte(4, ppm_v));

  EEPROM.put(NUMBER_STATISTICS * 0 + indexStatistic, ConvertToByte(1, t_inn));
  EEPROM.put(NUMBER_STATISTICS * 1 + indexStatistic, ConvertToByte(2, nrfRequest.tOut));
  EEPROM.put(NUMBER_STATISTICS * 2 + indexStatistic, ConvertToByte(3, h_v));
  EEPROM.put(NUMBER_STATISTICS * 3 + indexStatistic, ConvertToByte(4, ppm_v));
  EEPROM.put(NUMBER_STATISTICS * 4 + indexStatistic, ConvertToByte(5, nrfRequest.p_v));

  indexStatistic = (indexStatistic < (NUMBER_STATISTICS - 1) ? indexStatistic + 1 : 0);  //доходим до NUMBER_STATISTICS и затем снова начинаем с 0
  EEPROM.put(EEPROM_ADR_INDEX_STATISTIC, indexStatistic);
}

byte ConvertToByte(byte mode, float val)
{
  switch (mode)
  {
    case 1: //T inn
      return ((byte)(val * 10));
      break;
    case 2: //T out  от -32 до +32
      return ((byte)((32 + val) * 4));
      break;
    case 3: //Влажн
      return ((byte)val);
      break;
    case 4: //CO2
      return ((byte)(val / 10));
      break;
    case 5: //P
      return ((byte)(val - 7200) / 3);
      break;
  }
}

void ReadFromEEPROM(byte nMode)
{
  //  Serial.println("");
  //  Serial.print("ReadFromEEPROM. Mode =");
  //  Serial.println(nMode);
  for (byte i = 0; i < NUMBER_STATISTICS; i++)
  {
    EEPROM.get(NUMBER_STATISTICS * (nMode - 1) + i, values[i]);
    //    if (nMode == 4)//ppm_v
    //    {
    //      Serial.print(i);
    //      Serial.print(" ");
    //      Serial.println(values[i]);
    //    }
  }
}

//Get T out, Pressure and Command
void ReadCommandNRF()
{
  if (readCommandNRF_ms > READ_COMMAND_NRF_INTERVAL_S * 1000)
  {
    bool done = false;
    if ( radio.available() )
    {
      int cntAvl = 0;
      Serial.println("radio.available!!");
      _delay_ms(20);
      while (!done) {                            // Упираемся и
        done = radio.read(&nrfRequest, sizeof(nrfRequest)); // по адресу переменной nrfRequest функция записывает принятые данные
        _delay_ms(20);
        Serial.println("radio.read: ");
        Serial.println(nrfRequest.Command);
        Serial.println(nrfRequest.minutes);
        Serial.println(nrfRequest.tOut);
        _delay_ms(20);
        cntAvl++;
        if (cntAvl > 10)
        {
          Serial.println("powerDown");
          _delay_ms(20);
          radio.powerDown();
          radio.powerUp();
        }
      }
      if (nrfRequest.Command != RQ_NO) {
        HandleInputNrfCommand();
      };
      //radio.startListening();   // Now, resume listening so we catch the next packets.
      nrfResponse.Command == RSP_NO;
      nrfResponse.ventSpeed = 0;
    }
    readCommandNRF_ms = 0;
  }
}

void HandleInputNrfCommand()
{
  alertedRooms = "";
  if (nrfRequest.alarmMaxStatus > 0)
  {
    for (byte iCheckRoom = 0; iCheckRoom <= 5; iCheckRoom++)  //сформируем строку с номерами комнат с тревогой
    {
      if (bitRead(nrfRequest.alarmRooms, iCheckRoom))
      {
        alertedRooms = alertedRooms + (String)(iCheckRoom + 1);
      }
    }
    switch (nrfRequest.alarmMaxStatus)
    {
      case 1:
        Alarm(1);
        break;
      case 2:
        Alarm(2);
        break;
    }
  }

  nrfRequest.Command = RQ_NO;
}

//send room data and set Vent On/Off
void PrepareCommandNRF(byte command, byte ventSpeed, float t_set, byte scenarioVent, byte scenarioNagrev)
{
  Serial.println("PrepareCommandNRF1");
  if (nrfResponse.Command == RSP_NO || command == RSP_COMMAND) //не ставить RSP_INFO пока не ушло RSP_COMMAND
  {
    nrfResponse.Command = command;
  }
  nrfResponse.roomNumber = ROOM_NUMBER;
  nrfResponse.ventSpeed = ventSpeed;  //auto
  nrfResponse.t_set = t_set;   //auto
  nrfResponse.scenarioVent = scenarioVent;
  nrfResponse.scenarioNagrev = scenarioNagrev;
  nrfResponse.tInn = t_inn;
  nrfResponse.co2 = ppm_v;
  nrfResponse.h = h_v;

  uint8_t f = radio.flush_tx();
  radio.writeAckPayload(1, &nrfResponse, sizeof(nrfResponse));          // Pre-load an ack-paylod into the FIFO buffer for pipe 1
}

//Alarm alarmType== - внутренний, разовый; alarmType=1,2 - при каждом новом request от CentralControl
void Alarm(byte alarmType)  //0 - 0.2s short signal; 1 - 1s signal; 2 - 5s signal
{
  digitalWrite(BZZ_PIN, HIGH);
  delay(alarmType == 0 ? 200 : alarmType == 1 ? 1000 : 5000);
  digitalWrite(BZZ_PIN, LOW);
}

void CheckIR()
{
  decode_results irResult;
  if (irrecv.decode(&irResult))
  {
    String res = String(irResult.value, HEX);
    byte ventSpeed = 0;   //0-not supported, 1-1st speed, 2-2nd speed, 10 - off, 100 - auto

    Serial.print("IR: ");
    Serial.println(res);
    if (res == IR_CODE_VENT_AUTO)
      ventSpeed = 100;
    else if (res == IR_CODE_VENT1)
      ventSpeed = 1;
    else if (res == IR_CODE_VENT_AUTO)
      ventSpeed = 2;
    else if (res == IR_CODE_VENT_STOP)
      ventSpeed = 10;

    if (ventSpeed > 0)
    {
      Alarm(0);
      PrepareCommandNRF(RSP_COMMAND, ventSpeed, -100, 99, 99);
    }
  }
}

void SetLed()
{
  if (setLed_ms > SET_LED_INTERVAL_S * 1000)
  {
    int lightLevel = analogRead(LGHT_SENSOR_PIN);
    digitalWrite(LED_PIN, (lightLevel > LIGHT_LEVEL_DARK));
    setLed_ms = 0;
  }
}

void loop()
{
  ReadCommandNRF(); //each loop try read t_out and other info from central control

  RefreshSensorData();
  ChangeStatistic();
  //CheckIR();
  //SetLed();
  wdt_reset();
}

void DrawRect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int color, bool fill)
{
  if (fill)
    TFTscreen.fillRect(x1, y1, x2, y2, color);
  else
    TFTscreen.drawRect(x1, y1, x2, y2, color);
}

void PrintText(String text, int16_t x, int16_t y)
{
  Serial.println("PrintText");
  Serial.println(text);
  //TFTscreen.text(text, x, y);

  TFTscreen.setCursor(x, y);             // Определяем координаты верхнего левого угла области вывода
  TFTscreen.print(text);
}
