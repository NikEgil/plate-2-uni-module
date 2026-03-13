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

Preferences preferences;

HardwareSerial simSerial(1);
// EspSoftwareSerial::UART simSerial;
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
	byte t[5] = {0x00, 0x24, 0x00, 0x02, 0x00};
	preferences.begin("my-data", false); // Открываем пространство имен "my-data"
	// preferences.putBytes("array",t, 8);
	preferences.putBytes("array", tableSens, 8);

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

void SIM_activate(bool act)
{
	if (act)
	{
		simSerial.begin(115200, SERIAL_8N1, 39, 40);
		Serial.println("RS1 activate");
	}
	else
	{
		simSerial.end();
		Serial.println("RS1 DEactivate");
	}
	delay(500);
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
	configuration.OPTION.transmissionPower = POWER_10;

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

void setup()
{
	pinMode(LED_PIN, OUTPUT);
	// digitalWrite(LED_PIN, HIGH);
	// Serial.setTimeout(10000);
	Serial.begin(115200); // монитор порта
	while (!Serial)
	{
	}

	delay(200);

	Serial.println("hello"); // Startup all pins and UART
	SIM_activate(true);
	delay(200);

	simSerial.write("AT+GSN\r");
	String input = simSerial.readString();
	Serial.print("Вы ввели: ");
	Serial.println(input);
}

void loop()
{

	if (Serial.available())
	{
		digitalWrite(LED_PIN, HIGH);
// String input = Serial.readString();
		String input = Serial.readStringUntil('\n');
		Serial.print("Вы ввели: ");
		Serial.println(input);
		input=input+'\r';
		int len = input.length();
		char buffer[len + 1];
		input.toCharArray(buffer, len + 1);
		simSerial.write((uint8_t*)buffer, len);
		digitalWrite(LED_PIN, LOW);
	}
	if (simSerial.available())
	{
		Serial.write(simSerial.read());
	}
	
	}
// 	if (simSerial.available())
// 	{
// 		Serial.write(simSerial.read());
// 	}
// 	if (Serial.available())
// 	{
// 		simSerial.write(Serial.read());
// 	}
// }

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
			SIM_activate(true);
			GetDataMeteo();
			SIM_activate(false);
		}
		else
		{
			Serial.print("Work with port:	");
			Serial.println(port);
			SIM_activate(true);
			port_activate(port, true);

			Serial.println("1		port measure:");
			lenpac = sensReg[(int)tableSens[port]] * 2;
			byte measure[lenpac] = {};
			GetDataSens(port, measure, lenpac);

			Serial.println(lenpac);
			Serial.println("		pocket");

			appendData(totalData, position, port, lenpac, measure);

			port_activate(port, false);
			SIM_activate(false);
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