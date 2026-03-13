#include <Arduino.h>
#include <HardwareSerial.h>
#include "LoRa_E220.h"
#include <esp_sleep.h>
#include <Preferences.h>

const int ID = 8;
float Battery = 146;

#define LED_PIN 15
const gpio_num_t button1 = GPIO_NUM_13;
// #define button1 T12
const gpio_num_t button2 = GPIO_NUM_12;
byte tableSens[5] = {};

int sensReg[] =
	{0, 4, 4, 4, 4, 4, 4, 14, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 18, 0, 0, 0};

Preferences preferences;

HardwareSerial rs485Serial(1);
// EspSoftwareSerial::UART rs485Serial;
#define uS_TO_S_FACTOR 1000000 // Конверсия микросекунд в секунды
#define TIME_TO_SLEEP 60 * 15  // Время сна в секундах
#define TIME_TO_SLEEP_long 60 * 60 * 12
#define TIME_TO_SLEEP_error 5
// Serial1.begin(115200, SERIAL_8N1, 16, 17);
LoRa_E220 e220ttl(&Serial1, 8, 18, 21); //  RX TX AUX M0 M1

String readRS485Response();

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

void saveArrayToFlash()
{
	byte t[5]= {0x00,0x24,0x00,0x02,0x00};
	preferences.begin("my-data", false); // Открываем пространство имен "my-data"
	// preferences.putBytes("array",t, 8);
	preferences.putBytes("array",tableSens, 8);

	preferences.end();
	Serial.print("tableSens:		");
	printHEX(tableSens, 5);
}

bool loadArrayFromFlash()
{
	preferences.begin("my-data", true); // Открыть в режиме чтения
	size_t len = preferences.getBytes("array", tableSens, 8);
	preferences.end();

	if (len == 8)
	{
		Serial.println("Данные успешно загружены из памяти");
		return true;
	}
	else
	{
		Serial.println("Данные не найдены, инициализируем дефолтные значения");
		return false;
	}
}

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

void port_activate(int port, bool act)
{
	pinMode(port, OUTPUT);
	if (act)
	{
		digitalWrite(port, HIGH);
	}
	else
	{
		digitalWrite(port, LOW);
	}
	Serial.print("port  ");
	Serial.print(port);
	Serial.print("  state  ");
	Serial.println(act);
	delay(500);
}

void RS485_1_activate(bool act)
{
	if (act)
	{
		rs485Serial.begin(9600, SERIAL_8N1, 39, 40);
		Serial.println("RS1 activate");
	}
	else
	{
		rs485Serial.end();
		Serial.println("RS1 DEactivate");
	}
	delay(500);
}

void RS485_2_activate(bool act)
{
	if (act)
	{
		rs485Serial.begin(9600, SERIAL_8N1, 37, 38);
		Serial.println("RS2 activate");
	}
	else
	{
		rs485Serial.end();
		Serial.println("RS2 DEactivate");
	}
	delay(200);
}

void LORA_config_get()
{
	Serial.println("lora config get");

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
	Serial.println("lora config set");

	ResponseStructContainer c;
	c = e220ttl.getConfiguration();
	Configuration configuration = *(Configuration *)c.data;
	configuration.CHAN = chanel;
	configuration.ADDL = 0x03;
	configuration.ADDH = 0x00;
	configuration.SPED.uartBaudRate = UART_BPS_9600;
	configuration.SPED.airDataRate = AIR_DATA_RATE_010_24;
	// AIR_DATA_RATE_010_24
	configuration.SPED.uartParity = MODE_00_8N1;

	configuration.OPTION.subPacketSetting = SPS_200_00;
	configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_ENABLED;
	configuration.OPTION.transmissionPower = POWER_22;

	configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
	configuration.TRANSMISSION_MODE.fixedTransmission = 0;
	configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;
	configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;

	ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
	Serial.println(rs.getResponseDescription());
	Serial.println(rs.code);
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
	request[dataLength] = crc & 0xFF;			 // LSB
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
	outcrc[0] = crc & 0xFF;		   // LSB
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

	rs485Serial.flush();
	delay(100);
	rs485Serial.write(data, len);
	rs485Serial.flush();
}

bool getSOILdata(byte dataSOIL[])
{

	byte req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07};
	int lenreq = 6;
	byte request[lenreq + 2];
	int lenresponse = 32;
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

void LORA_activate(bool act)
{
	if (act)
	{
		e220ttl.begin();
	}
	else
	{
		Serial1.end();
	}
	Serial.print("lora state	");
	Serial.println(act);
	delay(500);
}

bool LORA_messedge_send(byte datatosend[], int lendatatosend)
{
	LORA_activate(true);
	ResponseStatus s;
	s = e220ttl.sendMessage(datatosend, lendatatosend);
	Serial.println(s.code);
	Serial.println(s.getResponseDescription());
	LORA_activate(false);
}

bool LORA_messedge_get()
{
	Serial.println("wait mes");
	for (int i = 0; i < 40; i++)
	{
		Serial.println(i);
		delay(200);
		if (e220ttl.available() > 1)
		{
			ResponseContainer rc = e220ttl.receiveMessage();
			if (rc.status.code != 1)
			{
				Serial.println(rc.status.getResponseDescription());
			}
			else
			{
				Serial.println(rc.status.getResponseDescription());
				byte newID[3];
				byte myID[3] = {};
				int length = rc.data.length();
				myID[0] = (byte)(ID >> 16) & 0xFF;
				myID[1] = (byte)(ID >> 8) & 0xFF;
				myID[2] = (byte)(ID) & 0xFF;
				// Преобразуем строку в массив байтов
				for (int i = 0; i < 3; i++)
				{
					newID[i] = (byte)rc.data.charAt(i);
				}
				printHEX(newID, 3);

				if (myID[3] == newID[3])
				{
					Serial.println(" OK Recive");
					return true;
				}
			}
		}
	}
	return false;
}

bool LORA_messedge_check()
{
	byte pak[3] = {};
	pak[0] = (byte)(ID >> 16) & 0xFF;
	pak[1] = (byte)(ID >> 8) & 0xFF;
	pak[2] = (byte)(ID) & 0xFF;
	pak[3] = 0x22;
	LORA_activate(true);
	ResponseStatus s;
	s = e220ttl.sendMessage(pak, 4);
	Serial.println(s.code);
	Serial.println(s.getResponseDescription());
	if (LORA_messedge_get())
	{
		Serial.println("OK");
		return true;
	}
	else
	{
		Serial.println("not OK");
		return false;
	}
}

bool searchmulti(int port)
{

	byte req1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
	int lenresponse = 7;
	byte response[lenresponse] = {};
	sendRS485Data(req1, 8);
	delay(500);
	rs485Serial.readBytes(response, lenresponse);
	rs485Serial.flush();
	Serial.println("founding multisens");
	printHEX(req1, lenresponse);
	printHEX(response, lenresponse);
	if (response[0] == 0x01)
	{
		Serial.println("ret true");
		return true;
	}
	else
	{
		Serial.println("ret false");
		return false;
	}
}

bool searchSensors_1()
{
	pinMode(LED_PIN, LOW);
	int port = 1;
	LORA_activate(false);
	port_activate(7, true);
	port_activate(port, true);
	RS485_1_activate(true);

	delay(5000);

	Serial.println("Search sensors, port 	1");
	Serial.println("	request:");

	byte req1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
	int lenresponse1 = 7;
	byte response1[lenresponse1] = {};
	sendRS485Data(req1, 8);
	delay(500);
	rs485Serial.readBytes(response1, lenresponse1);
	rs485Serial.flush();
	Serial.println("founding multisens");
	printHEX(req1, lenresponse1);
	printHEX(response1, lenresponse1);
	if (response1[0] == 0x01)
	{
		tableSens[port] = response1[0];
		Serial.println("multisens is FOUND!!!");
		port_activate(7, false);
		port_activate(port, false);
		RS485_1_activate(false);
		saveArrayToFlash();
		blink(1, 1000);
		return true;
	}

	byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};

	int lenreq = 8;
	int lenresponse = 32;
	byte response[lenresponse] = {};

	printHEX(req, lenreq);
	sendRS485Data(req, lenreq);
	delay(500);
	rs485Serial.readBytes(response, lenresponse);
	rs485Serial.flush();
	printHEX(response, lenresponse);
	if (response[0] == 0x00)
	{
		for (int i = 0; i < 40; i++)
		{
			delay(500);
			rs485Serial.readBytes(response, lenresponse);
			rs485Serial.flush();
			printHEX(response, lenresponse);
			if (response[0] != 0)
			{
				break;
			}
		}
	}
	Serial.println("	response:");
	printHEX(response, lenresponse);
	if (response[0] != 0)
	{
		Serial.print((int)response[0]);
		Serial.println("		FOUND		!!!");
		if (response[0] == 0x24)
		{
			Serial.println("meteostation detected");
		}
	}
	else
	{
		Serial.println("		NOT SENS	!!!");
	}
	tableSens[port] = response[0];

	port_activate(7, false);
	port_activate(port, false);
	RS485_1_activate(false);
	saveArrayToFlash();
	if (response[0] != 0x00)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool searchSensors_2()
{
	Serial.println("	SEARCH SENS 234");
	LORA_activate(false);
	pinMode(LED_PIN, LOW);
	port_activate(7, true);

	RS485_2_activate(true);

	for (int port = 3; port < 4; port++)
	{
		port_activate(port, true);
		// delay(5000000);
		delay(5000);
		byte req1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
		int lenresponse1 = 7;
		byte response1[lenresponse1] = {};
		sendRS485Data(req1, 8);
		delay(500);
		rs485Serial.readBytes(response1, lenresponse1);
		rs485Serial.flush();
		Serial.println("founding multisens");
		printHEX(req1, lenresponse1);
		printHEX(response1, lenresponse1);
		if (response1[0] == 0x01)
		{
			tableSens[port] = response1[0];
			Serial.println("multisens is FOUND!!!");
			port_activate(7, false);
			RS485_2_activate(false);
			saveArrayToFlash();
			if (tableSens[port] != 0x00)
			{
				return true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};
			int lenreq = 8;
			int lenresponse = 32;
			byte response[lenresponse] = {};
			Serial.print("Search sensors, port 	2");
			Serial.println("	request:");
			printHEX(req, lenreq);
			sendRS485Data(req, lenreq);

			delay(500);
			rs485Serial.readBytes(response, lenresponse);
			rs485Serial.flush();

			Serial.println("	response:");
			printHEX(response, lenresponse);

			if (response[0] != 0)
			{
				Serial.print((int)response[0]);
				Serial.println("		FOUND		!!!");
				blink(port, 500);
			}
			else
			{
				Serial.println("		NOT SENS	!!!");
			}
			port_activate(port, false);

			tableSens[port] = response[0];
		}
		port_activate(7, false);
		RS485_2_activate(false);
		saveArrayToFlash();
		if (tableSens[port] != 0x00)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
}

bool GetDataSens(int port, byte measure[], int lenpac)
{
	byte req[] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x07};
	req[0] = tableSens[port];
	Serial.print("tableSens number:		");
	Serial.println((int)tableSens[port]);
	Serial.print("sensReg number:		");
	int lenresp = lenpac / 2;
	Serial.println(lenresp);

	req[5] = (byte)lenresp;
	int lenreq = 6;
	byte request[lenreq + 2];
	int lenresponse = 5 + lenpac;
	byte response[lenresponse];

	addCRC(req, lenreq, request);
	Serial.println("request:");
	printHEX(request, lenreq + 2);
	delay(5000);

	sendRS485Data(request, lenreq + 2);

	delay(300);

	rs485Serial.readBytes(response, lenresponse);
	Serial.println("response:");
	printHEX(response, lenresponse);
	if (response[0] == tableSens[port])
	{
		for (int i = 0; i < 4; i++)
		{
			measure[i] = response[i + 3];
		}
		return true;
	}
	else
	{
		return false;
	}
}

bool GetDataSens_multi(int port, byte measure[])
{
	byte req[] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x02};
	req[0] = (byte)port;
	Serial.print("sensReg number:		");
	int lenreq = 6;
	byte request[lenreq + 2];
	int lenresponse = 5 + 4;
	byte response[lenresponse];

	addCRC(req, lenreq, request);
	Serial.println("request:");
	printHEX(request, lenreq + 2);
	sendRS485Data(request, lenreq + 2);

	delay(300);

	rs485Serial.readBytes(response, lenresponse);
	Serial.println("response:");
	printHEX(response, lenresponse);
	if (response[0] == (byte)port)
	{
		for (int i = 0; i < 4; i++)
		{
			measure[i] = response[i + 3];
		}
		return true;
	}
	else
	{
		return false;
	}
}

void parseWeatherData(const byte data[], byte meteo[])
{
	int windDir = data[2] + (((uint16_t)(data[3] & 0x80)) << 1);
	Serial.print("windDir		");
	Serial.println(windDir);

	uint16_t tmp_val = data[4] + (((uint16_t)(data[3] & 0x07)) << 8);
	float temperature = (tmp_val != 0x7FF) ? (tmp_val - 400) / 10.0 : 0;
	Serial.print("temperature	");
	Serial.println(temperature);

	uint8_t humidity = data[5];
	Serial.print("humidity	");
	Serial.println(humidity);

	uint16_t wind_speed_val = data[6]; // + (((uint16_t)(data[3] & 0x10)) << 4);
	float wind_speed = (wind_speed_val != 0x1FF) ? wind_speed_val / 8.0 * 0.51 : 0;
	Serial.print("wind_speed	");
	Serial.println(wind_speed);

	uint8_t wind_gust = (uint16_t)data[7] * 0.51;
	Serial.print("wind_gust	");
	Serial.println(wind_gust);

	float rainfall = data[9] + (((uint16_t)data[8]) << 8);
	rainfall = rainfall * 0.254;
	Serial.print("raifall		");
	Serial.println(rainfall);

	float uv = data[11] + (((uint16_t)data[10]) << 8);
	Serial.print("uv		");
	Serial.println(uv);

	uint32_t light_val = (data[14] + (data[13] << 8) + (data[12] << 16));
	uint32_t light = (light_val != 0xFFFFFF) ? light_val / 10.0 : 0;
	Serial.print("light		");
	Serial.println(light);

	uint32_t pres_val = (data[19] + (data[18] << 8) + (data[17] << 16));
	float pres = (pres_val != 0xFFFFFF) ? pres_val / 100.0 : 0;
	Serial.print("press		");
	Serial.println(pres);

	float floatArray[9] = {temperature * 10.0f, humidity, pres * 10.0f, windDir, wind_speed, wind_gust, uv, light / 5, rainfall * 100.0f};
	// {windDir * 10.0f, temperature * 10.0f, humidity * 10.0f, wind_speed * 10.0f, wind_gust * 10.0f, rainfall * 10.0f};

	for (size_t i = 0; i < 9; i++)
	{
		uint16_t value = (uint16_t)floatArray[i]; // Преобразуем float в uint16_t (отбрасываем дробную часть)

		// Разбиваем на 2 байта в Big Endian
		meteo[i * 2] = (value >> 8) & 0xFF; // Старший байт
		meteo[i * 2 + 1] = value & 0xFF;	// Младший байт
	}
	Serial.println("measured array:");
}

bool GetDataMeteo(byte measure[])
{
	Serial.println("Wait meteo data");
	int lenresponse = 32;
	byte response[lenresponse] = {};
	for (int i = 0; i < 100; i++)
	{
		delay(200);
		rs485Serial.readBytes(response, lenresponse);
		rs485Serial.flush();
		Serial.println("	response:");
		printHEX(response, lenresponse);

		if (response[0] == 0x24)
		{
			parseWeatherData(response, measure);
			printHEX(measure, 18);
			return true;
		}
	}
	return false;
}

void appendData(byte totalData[], int &position, int port, int lenpac, byte measure[])
{
	totalData[position] = (byte)port;
	totalData[position + 1] = byte(tableSens[port]);
	position += 2;
	for (int i = 0; i < lenpac; i++)
	{
		totalData[position + i] = measure[i];
	}
	position += lenpac;
}

void appendData_multi(byte totalData[], int &position, int port, int sens, int lenpac, byte measure[])
{
	totalData[position] = (byte)port;
	totalData[position + 1] = (byte)sens;
	totalData[position + 2] = (byte)lenpac;
	position += 3;
	for (int i = 0; i < lenpac; i++)
	{
		totalData[position + i] = measure[i];
	}
	position += lenpac;
}

void sleep(int time)
{
	esp_sleep_enable_timer_wakeup(time * uS_TO_S_FACTOR);
	Serial.print("Переход в глубокий сон...на ");
	Serial.println(time);
	delay(500);
	esp_deep_sleep_start();
}

void port_activate(bool act)
{
	if (act == true)
	{
		port_activate(7, true);
		port_activate(1, true);
		port_activate(2, true);
		port_activate(3, true);
		port_activate(4, true);
	}
	else
	{

		port_activate(7, false);
		port_activate(1, false);
		port_activate(2, false);
		port_activate(3, false);
		port_activate(4, false);
	}
}

void sender()
{
	LORA_activate(true);
	delay(200);

	ResponseStatus s;
	byte combinedData[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x11};
	for (int i = 0; i < 10; i++)
	{
		s = e220ttl.sendMessage(combinedData, 10);
		Serial.println(s.code);
		Serial.println(s.getResponseDescription());
		delay(5000);
	}
	delay(5000);
}

int getBattery()
{
	pinMode(5, INPUT);

	// Опционально: настройка разрешения ADC
	analogReadResolution(13);
	float adcValue = analogRead(5);

	long sum = 0;

	for (int i = 0; i < 10; i++)
	{
		sum += analogRead(5);
		delay(20); // Небольшая задержка между измерениями
	}
	// 11 	5.9079
	// 1 	1.74595
	// 2 	.016176
	// 3 4 	5.71
	// 5 1.54981412
	// 6	1.7885106
	// 7	5.6121621
	// 8	5.8605633
	// 9	5.8436619
	// 10 5.86478873
	float coef = 5.8605633;
	float average = (float)sum / 10;
	float pinVoltage = (average * 2.69) / 8191.0;
	float realVoltage = pinVoltage * coef;
	float v = ((realVoltage - 3) / 1.2) * 100.0;
	Serial.print("avg: ");
	Serial.print(average);
	Serial.print("	pin: ");
	Serial.print(pinVoltage);
	Serial.print("	real: ");
	Serial.print(realVoltage);
	Serial.print("	perc: ");

	Serial.println(v);
	return (int)v;
}

void actionTimer()
{
	port_activate(7, true);

	int lenpac;
	int totallen = 7;

	for (int i = 1; i < 5; i++)
	{
		if (tableSens[i] != 0)
		{
			if (tableSens[i] == 0x01)
			{
				totallen += 3 * 5;
				totallen += sensReg[(int)tableSens[i]] * 5;
			}
			else
			{
				totallen += 3;
				totallen += sensReg[(int)tableSens[i]];
			}
		}
	}
	Serial.println(totallen);
	byte totalData[198] = {};

	totalData[0] = (byte)(ID >> 16) & 0xFF;
	totalData[1] = (byte)(ID >> 8) & 0xFF;
	totalData[2] = (byte)(ID) & 0xFF;
	totalData[3] = (byte)Battery;

	totalData[4] = (byte)0;

	int position = 5;

	int port = 1;
	if (tableSens[port] != 0x00)
	{
		Serial.print("Work with port:	");
		Serial.println(port);
		RS485_1_activate(true);
		port_activate(port, true);
		delay(5000);
		if (tableSens[port] == 0x24)
		{

			lenpac = sensReg[(int)tableSens[port]];
			byte measure[lenpac] = {};
			if (GetDataMeteo(measure))
			{
				Serial.println(lenpac);
				Serial.println("		pocket");
				appendData_multi(totalData, position, port, tableSens[port], lenpac, measure);
			}
			else
			{
				Serial.print("		ERROR	!!!!");
			}
		}
		else if (tableSens[port] == 0x01)
		{
			for (int sens = 1; sens < 6; sens++)
			{
				lenpac = sensReg[(int)tableSens[port]];
				byte measure[lenpac] = {};
				if (GetDataSens_multi(sens, measure))
				{

					Serial.println(lenpac);
					Serial.println("		pocket");

					appendData_multi(totalData, position, port, sens, lenpac, measure);
				}
				else
				{
					Serial.print("		ERROR	!!!!");
				}
			}
		}
		else
		{
			Serial.println("1		port measure:");
			lenpac = sensReg[(int)tableSens[port]];
			byte measure[lenpac] = {};
			if (GetDataSens(port, measure, lenpac))
			{

				Serial.println(lenpac);
				Serial.println("		pocket");

				appendData_multi(totalData, position, port, tableSens[port], lenpac, measure);
			}
			else
			{
				Serial.print("		ERROR	!!!!");
			}
			port_activate(port, false);
			RS485_1_activate(false);
		}
	}

	RS485_2_activate(true);
	for (int port = 3; port < 4; port++)
	{
		if (tableSens[port] != 0x00)
		{
			port_activate(port, true);
			Serial.print(port);
			Serial.println("		port measure:");
			if (tableSens[port] == 0x01)
			{
				Serial.println("work with multisens");
				for (int sens = 1; sens < 6; sens++)
				{
					lenpac = sensReg[(int)tableSens[port]];
					byte measure[lenpac] = {};
					GetDataSens_multi(sens, measure);

					Serial.println(lenpac);
					Serial.println("		pocket");

					appendData_multi(totalData, position, port, sens, lenpac, measure);
				}
			}
			else
			{
				lenpac = sensReg[(int)tableSens[port]];

				byte measure[lenpac] = {};
				GetDataSens(port, measure, lenpac);

				Serial.println(lenpac);
				Serial.println("		pocket");

				appendData_multi(totalData, position, port, tableSens[port], lenpac, measure);
			}
			port_activate(port, false);
		}
	}
	RS485_2_activate(false);
	port_activate(7, false);

	byte crc[] = {};
	Serial.println(position);
	int step = position + 2;
	outCRC(totalData, position, crc);
	printHEX(crc, 2);
	Serial.println(position);
	Serial.println(step);

	totalData[step - 2] = crc[0];
	totalData[step - 1] = crc[1];
	Serial.println("DATA PACKET");
	printHEX(totalData, totallen);
	LORA_activate(true);
	ResponseStatus s;
	s = e220ttl.sendMessage(totalData, 198);
	Serial.println(s.code);
	Serial.println(s.getResponseDescription());

	if (LORA_messedge_get())
	{
		blink(1, 2000);
	}
	else
	{
		blink(8, 350);
	}
	LORA_activate(false);
}

void setup()
{
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, HIGH);

	Serial.begin(115200); // монитор порта
	// for (int i fn= 0; i < 10; i++) {if (!Serial){}}
	while (!Serial)
	{
	}
	delay(200);
	Serial.println("hello"); // Startup all pins and UART
	pinMode(button1, INPUT_PULLUP);
	pinMode(button2, INPUT_PULLUP);
	Battery = getBattery();
	if (Battery < 5)
	{
		sleep(TIME_TO_SLEEP_long);
	}
	// Попытка загрузить массив из памяти
	if (!loadArrayFromFlash())
	{
		for (int i = 0; i < 5; i++)
		{
			tableSens[i] = 0x00; // Заполняем 0,1,2...7
		}
		saveArrayToFlash();
	}
	printHEX(tableSens, 5);

	// Serial.println("Sens table");
	// printHEX(tableSens, 8);

	if (digitalRead(button1) == LOW)
	{
		Serial.println("Кнопка BUTTON 1 нажата!");
		digitalWrite(LED_PIN, LOW);
		for (int i = 0; i < 5; i++)
		{
			tableSens[i] = 0x00;
		}
		tableSens[0]=0x00;
		tableSens[1]=0x24;
		tableSens[2]=0x00;
		tableSens[3]=0x02;
		tableSens[4]=0x00;
		saveArrayToFlash();
		LORA_activate(true);
		LORA_config_set(19);
		LORA_config_get();
		LORA_activate(false);

		// if (searchSensors_1())
		// {
		// 	Serial.println("blink");
		// 	pinMode(LED_PIN, OUTPUT);

		// 	digitalWrite(LED_PIN, HIGH);
		// 	delay(1000);
		// 	digitalWrite(LED_PIN, LOW);
		// } // Выпqолняем нужную функцию
		// if (searchSensors_2())
		// {
		// 	Serial.println("blink");
		// 	pinMode(LED_PIN, OUTPUT);

		// 	digitalWrite(LED_PIN, HIGH);
		// 	delay(1000);
		// 	digitalWrite(LED_PIN, LOW);
		// }
	}
	else if (digitalRead(button2) == LOW)
	{
		Serial.println("Кнопка BUTTON 2 нажата!");
		digitalWrite(LED_PIN, LOW);
		if (LORA_messedge_check())
		{
			blink(1, 2000);
		}
		else
		{
			blink(4, 350);
		}
		LORA_activate(false);
	}
	else
	{
		actionTimer();
	}
	// sleep(5);
	sleep(TIME_TO_SLEEP);
}

void loop()
{
}

/*

void actionTimer222()
{
	port_activate(7, true);

	int lenpac;
	int totallen = 7;

	for (int i = 1; i < 5; i++)
	{
		if (tableSens[i] != 0)
		{
			totallen += 2;
			totallen += sensReg[(int)tableSens[i]] * 2;
		}
	}
	Serial.println(totallen);
	byte totalData[198] = {};

	totalData[0] = (byte)(ID >> 16) & 0xFF;
	totalData[1] = (byte)(ID >> 8) & 0xFF;
	totalData[2] = (byte)(ID) & 0xFF;
	totalData[3] = (byte)Battery;

	totalData[4] = (byte)0;

	int position = 5;
	int port = 1;
	if (tableSens[port] != 0x00)
	{
		Serial.print("Work with port:	");
		Serial.println(port);
		if (tableSens[port] == 0x24)
		{
			RS485_1_activate(true);
			GetDataMeteo();
			RS485_1_activate(false);
		}
		else
		{
			Serial.print("Work with port:	");
			Serial.println(port);
			RS485_1_activate(true);
			port_activate(port, true);

			Serial.println("1		port measure:");
			lenpac = sensReg[(int)tableSens[port]] * 2;
			byte measure[lenpac] = {};
			GetDataSens(port, measure, lenpac);

			Serial.println(lenpac);
			Serial.println("		pocket");

			appendData(totalData, position, port, lenpac, measure);

			port_activate(port, false);
			RS485_1_activate(false);
		}
	}

	RS485_2_activate(true);
	for (int port = 2; port < 5; port++)
	{
		if (tableSens[port] != 0x00)
		{
			port_activate(port, true);
			Serial.print(port);
			Serial.println("		port measure:");
			lenpac = sensReg[(int)tableSens[port]] * 2;

			byte measure[lenpac] = {};
			GetDataSens(port, measure, lenpac);

			Serial.println(lenpac);
			Serial.println("		pocket");

			appendData(totalData, position, port, lenpac, measure);

			port_activate(port, false);
		}
	}
	RS485_2_activate(false);
	port_activate(7, false);

	byte crc[] = {};
	Serial.println(position);
	int step = position + 2;
	outCRC(totalData, position, crc);
	printHEX(crc, 2);
	Serial.println(position);
	Serial.println(step);

	totalData[step - 2] = crc[0];
	totalData[step - 1] = crc[1];
	Serial.println("DATA PACKET");
	printHEX(totalData, totallen);
	LORA_activate(true);
	ResponseStatus s;
	s = e220ttl.sendMessage(totalData, 198);
	Serial.println(s.code);
	Serial.println(s.getResponseDescription());
	LORA_activate(false);
}*/