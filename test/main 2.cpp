#include <Arduino.h>
#include <HardwareSerial.h>
#include "LoRa_E220.h"

#define MySerial Serial2
#define LED_PIN 2
// HardwareSerial rs485Serial(1);
#define rs485Serial Serial1

LoRa_E220 e220ttl(&MySerial, 15, 21, 19); //  RXTX AUX M0 M1
String readRS485Response();

const int ID = 215;

void printParameters(struct Configuration configuration)
{
  Serial.println("----------------------------------------");

  Serial.print(F("HEAD : "));
  Serial.print(configuration.COMMAND, HEX);
  Serial.print(" ");
  Serial.print(configuration.STARTING_ADDRESS, HEX);
  Serial.print(" ");
  Serial.println(configuration.LENGHT, HEX);
  Serial.println(F(" "));
  Serial.print(F("AddH : "));
  Serial.println(configuration.ADDH, HEX);
  Serial.print(F("AddL : "));
  Serial.println(configuration.ADDL, HEX);
  Serial.println(F(" "));
  Serial.print(F("Chan : "));
  Serial.print(configuration.CHAN, DEC);
  Serial.print(" -> ");
  Serial.println(configuration.getChannelDescription());
  Serial.println(F(" "));
  Serial.print(F("SpeedParityBit     : "));
  Serial.print(configuration.SPED.uartParity, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.SPED.getUARTParityDescription());
  Serial.print(F("SpeedUARTDatte     : "));
  Serial.print(configuration.SPED.uartBaudRate, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.SPED.getUARTBaudRateDescription());
  Serial.print(F("SpeedAirDataRate   : "));
  Serial.print(configuration.SPED.airDataRate, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.SPED.getAirDataRateDescription());
  Serial.println(F(" "));
  Serial.print(F("OptionSubPacketSett: "));
  Serial.print(configuration.OPTION.subPacketSetting, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.OPTION.getSubPacketSetting());
  Serial.print(F("OptionTranPower    : "));
  Serial.print(configuration.OPTION.transmissionPower, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.OPTION.getTransmissionPowerDescription());
  Serial.print(F("OptionRSSIAmbientNo: "));
  Serial.print(configuration.OPTION.RSSIAmbientNoise, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.OPTION.getRSSIAmbientNoiseEnable());
  Serial.println(F(" "));
  Serial.print(F("TransModeWORPeriod : "));
  Serial.print(configuration.TRANSMISSION_MODE.WORPeriod, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.TRANSMISSION_MODE.getWORPeriodByParamsDescription());
  Serial.print(F("TransModeEnableLBT : "));
  Serial.print(configuration.TRANSMISSION_MODE.enableLBT, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.TRANSMISSION_MODE.getLBTEnableByteDescription());
  Serial.print(F("TransModeEnableRSSI: "));
  Serial.print(configuration.TRANSMISSION_MODE.enableRSSI, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.TRANSMISSION_MODE.getRSSIEnableByteDescription());
  Serial.print(F("TransModeFixedTrans: "));
  Serial.print(configuration.TRANSMISSION_MODE.fixedTransmission, BIN);
  Serial.print(" -> ");
  Serial.println(configuration.TRANSMISSION_MODE.getFixedTransmissionDescription());

  Serial.println("----------------------------------------");
}
void printModuleInformation(struct ModuleInformation moduleInformation)
{
  Serial.println("----------------------------------------");
  Serial.print(F("HEAD: "));
  Serial.print(moduleInformation.COMMAND, HEX);
  Serial.print(" ");
  Serial.print(moduleInformation.STARTING_ADDRESS, HEX);
  Serial.print(" ");
  Serial.println(moduleInformation.LENGHT, DEC);

  Serial.print(F("Model no.: "));
  Serial.println(moduleInformation.model, HEX);
  Serial.print(F("Version  : "));
  Serial.println(moduleInformation.version, HEX);
  Serial.print(F("Features : "));
  Serial.println(moduleInformation.features, HEX);
  Serial.println("----------------------------------------");
}

void LORA_config_get()
{
  ResponseStructContainer c;
  c = e220ttl.getConfiguration();
  // It's important get configuration pointer before all other operation
  Configuration configuration = *(Configuration *)c.data;
  Serial.println(c.status.getResponseDescription());
  Serial.println(c.status.code);

  printParameters(configuration);
}

void LORA_config_set(int chanel)
{
  ResponseStructContainer c;
  c = e220ttl.getConfiguration();
  Configuration configuration = *(Configuration *)c.data;
  configuration.CHAN = chanel;
  configuration.ADDL = 0x03;
  configuration.ADDH = 0x00;
  configuration.SPED.uartBaudRate = UART_BPS_9600;
  configuration.SPED.airDataRate = AIR_DATA_RATE_010_24;
  configuration.SPED.uartParity = MODE_00_8N1;

  configuration.OPTION.subPacketSetting = SPS_200_00;
  configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_ENABLED;
  configuration.OPTION.transmissionPower = POWER_10;

  configuration.TRANSMISSION_MODE.enableRSSI = RSSI_DISABLED;
  configuration.TRANSMISSION_MODE.fixedTransmission = 0;
  configuration.TRANSMISSION_MODE.enableRSSI = false;
  configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;
  configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;

  ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  Serial.println(rs.getResponseDescription());
  Serial.println(rs.code);
}

void printHEX(byte data[], int len)
{
  for (int j = 0; j < len; j++)
  {
    Serial.print("0x");
    if (data[j] < 0x10)
      Serial.print("0"); // Добавляем ведущий ноль для однозначных HEX
    Serial.print(data[j], HEX);
    if (j < len + 1)
      Serial.print(", ");
  }
  Serial.println();
}

void addCRC(byte req[], int dataLength, byte request[])
{
  uint16_t crc = 0xFFFF;

  for (int pos = 0; pos < dataLength; pos++)
  {
    crc ^= (uint16_t)req[pos];
    for (int i = 8; i != 0; i--)
    {
      if ((crc & 0x0001) != 0)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }

  // Копируем исходные данные в результат
  for (int i = 0; i < dataLength; i++)
  {
    request[i] = req[i];
  }

  // Добавляем CRC в конец (младший байт первый)
  request[dataLength] = crc & 0xFF;            // LSB
  request[dataLength + 1] = (crc >> 8) & 0xFF; // MSB
}

void outCRC(byte req[], int dataLength, byte outcrc[])
{
  uint16_t crc = 0xFFFF;

  for (int pos = 0; pos < dataLength; pos++)
  {
    crc ^= (uint16_t)req[pos];
    for (int i = 8; i != 0; i--)
    {
      if ((crc & 0x0001) != 0)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }

  // Добавляем CRC в конец (младший байт первый)
  outcrc[0] = crc & 0xFF;        // LSB
  outcrc[1] = (crc >> 8) & 0xFF; // MSB
}

void dataPreparation(byte data[], int paramscode[], int lenparamcode, byte dataprepare[])
{
  int q = 0;
  int l = 0;
  for (int i = 0; i < lenparamcode; i++)
  {
    // Записываем param[i] (предполагаем, что params[i] помещается в byte)
    dataprepare[q] = (byte)paramscode[i];
    dataprepare[q + 1] = data[l];
    dataprepare[q + 2] = data[l + 1];
    q += 3;
    l += 2;
  }
}

void blink(int count, int delayy)
{
  for (int i = 0; i < count; i++)
  {
    digitalWrite(LED_PIN, HIGH);
    delay(delayy);
    digitalWrite(LED_PIN, LOW);
    delay(delayy);
  }
}

void sendRS485Data(byte *data, int len)
{
  rs485Serial.write(data, len);
  rs485Serial.flush();
}

bool getSOILdata(byte dataSOIL[])
{

  byte req[] = {0x0a, 0x03, 0x00, 0x00, 0x00, 0x07};
  ; // air 11
  int lenreq = 6;
  byte request[lenreq + 2];
  int lenresponse = 28;
  byte response[lenresponse] = {};
  int paramcode[] = {81, 80, 86, 85, 82, 83, 84};
  int lenparamcode = 7;
  addCRC(req, lenreq, request);

  Serial.println("request:");
  printHEX(request, lenreq + 2);

  sendRS485Data(request, lenreq + 2);
  delay(200);

  rs485Serial.readBytes(response, lenresponse);
  Serial.println("response:");
  printHEX(response, lenresponse);
  if (response[0] != 0x0a)
  {
    Serial.println("broken measure");
    return false;
  }

  byte trimmedData[14] = {};
  for (int i = 0; i < 14; i++)
  {
    trimmedData[i] = response[i + 3];
  }
  dataPreparation(trimmedData, paramcode, lenparamcode, dataSOIL);
  return true;
}

void setup()
{

  Serial.begin(115200); // монитор порта
  while (!Serial)
  {
  };
  delay(500);

  Serial.println("hello"); // Startup all pins and UART
  rs485Serial.begin(9600, SERIAL_8N1, 18, 5);
  delay(200);
  e220ttl.begin();
  //
  delay(200);
  LORA_config_set(19);
  delay(200);

  LORA_config_get();

  ResponseStatus rs = e220ttl.sendMessage("Hello, world?");
  // Check If there is some problem of succesfully send
  Serial.println(rs.getResponseDescription());
}

void byteArrayToHexString(const byte *byteArray, int length, String str)
{
  for (int i = 0; i < length; i++)
  {
    if (i > 0)
    {
      str += " ";
    }
    str += String(byteArray[i], HEX); // вывод в HEX
  }
}
bool LORA_messedge_send(byte *datatosend, int lendatatosend, int id, int battary)
{
  // Заголовок LoRa (адрес 0x0001, канал 0x2E)
  byte byteID[3] = {
      (byte)(ID >> 16) & 0xFF,
      (byte)(ID >> 8) & 0xFF, // Старший байт (0x86)
      (byte)(ID) & 0xFF       // Младший байт (0xCA)
  };
  byte byteBattary[] = {(byte)(battary)};
  byte crc[2] = {};
  outCRC(datatosend, lendatatosend, crc);

  const int totalSize = 3 + lendatatosend + 1 + 2; // 3 + 3 + 21 + 1 + 2 = 30 байт

  // Создаем буфер для объединенных данных
  byte combinedData[totalSize];

  // Копируем данные в буфер
  int position = 0;

  memcpy(combinedData + position, byteID, 3);
  position += 3;

  memcpy(combinedData + position, datatosend, lendatatosend);
  position += lendatatosend;

  memcpy(combinedData + position, byteBattary, 1);
  position += 1;

  memcpy(combinedData + position, crc, 2);
  // position += 2;

  Serial.println("sended data");
  printHEX(combinedData, totalSize);
  String senddata = "";
  byteArrayToHexString(combinedData, totalSize, senddata);

  ResponseStatus s;
  s = e220ttl.sendMessage(senddata);
  s = e220ttl.sendMessage("otpravleno");
  Serial.println(s.code);
  Serial.println(s.getResponseDescription());
  return true;
}

void LORA_messedge_get()
{
  Serial.println("wait mes");
  if (e220ttl.available() > 1)
  {
    Serial.println(".1");

    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code != 1)
    {
      Serial.println(rc.status.getResponseDescription());
    }
    else
    {
      Serial.println(rc.status.getResponseDescription());
      Serial.println(rc.data);
    }
  }
}

void loop()
{

  Serial.println("soil get");
  byte dataSOIL[21] = {};

  if (!getSOILdata(dataSOIL))
  {
    Serial.println("no soil");
  };

  Serial.println("lora");

  delay(200);
  // LORA_messedge_send(dataSOIL,21,ID,100);
  // ResponseStatus s;
  // // s=e220ttl.sendMessage(senddata);
  // s = e220ttl.sendMessage("otpravleno");
  // Serial.println(s.code);
  // Serial.println(s.getResponseDescription());
  ResponseStatus rs = e220ttl.sendMessage("Hello, world?");
  // Check If there is some problem of succesfully send
  Serial.println(rs.getResponseDescription());
  delay(2000);

  LORA_messedge_get();

  Serial.println("end");

  delay(5000);
}
