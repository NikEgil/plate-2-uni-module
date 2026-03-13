#include <Arduino.h>
#include <HardwareSerial.h>
#include "LoRa_E220.h"
#include <esp_sleep.h>
#include <Preferences.h>
#define TINY_GSM_MODEM_SIM800

#include <TinyGsmClient.h>
#include <StreamDebugger.h>

#include "defenitions.h"
const int ID = 8;
float Battery = 146;

const gpio_num_t button1 = GPIO_NUM_13;
const gpio_num_t button2 = GPIO_NUM_12;
byte tableSens[5] = {};
Preferences preferences;
// HardwareSerial SerialAT(1); // Using Serial1 on ESP32

// Modem power pin (if applicable, adjust for your board)
const int MODEM_PWR_PIN = 4;
const char *apn = "internet"; // Access Point Name
const char *gprsUser = "";	  // GPRS username (if required)
const char *gprsPass = "";	  // GPRS password (if required)

// Server details for data transmission
const char *server = "example.com";
const int port = 80;
// // Create a TinyGSM modem instance
// TinyGsm modem(SerialAT);
// // Create a TinyGSM client instance for network communication
// TinyGsmClient client(modem);

// #define MySerial Serial2
// HardwareSerial rs485Serial(1);
#define rs485Serial Serial1
// LoRa_E220 e220ttl(&MySerial, 15, 21, 19); //  RXTX AUX M0 M1
String readRS485Response();

// Serial1.begin(115200, SERIAL_8N1, 16, 17);
// LoRa_E220 e220ttl(&Serial1, 8, 18, 21); //  RX TX AUX M0 M1

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

void RS485_1_activate(bool act)
{
	if (act)
	{
		rs485Serial.begin(9600, SERIAL_8N1, RS1RX, RS1TX);
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
		rs485Serial.begin(9600, SERIAL_8N1, RS2RX, RS2TX);
		Serial.println("RS2 activate");
	}
	else
	{
		rs485Serial.end();
		Serial.println("RS2 DEactivate");
	}
	delay(200);
}

// void LORA_config_get()
// {
// 	Serial.println("lora config get");

// 	ResponseStructContainer c;
// 	c = e220ttl.getConfiguration();
// 	// It's important get configuration pointer before all other operation
// 	Configuration configuration = *(Configuration *)c.data;
// 	Serial.println(c.status.getResponseDescription());
// 	Serial.println(c.status.code);

// 	printParameters(configuration);
// }

// void LORA_config_set(int chanel, byte add)
// {
// 	Serial.println("lora config set");

// 	ResponseStructContainer c;
// 	c = e220ttl.getConfiguration();
// 	Configuration configuration = *(Configuration *)c.data;
// 	configuration.CHAN = chanel;
// 	configuration.ADDL = add;
// 	configuration.ADDH = add;
// 	configuration.SPED.uartBaudRate = UART_BPS_9600;
// 	configuration.SPED.airDataRate = AIR_DATA_RATE_010_24;
// 	// AIR_DATA_RATE_010_24
// 	configuration.SPED.uartParity = MODE_00_8N1;

// 	configuration.OPTION.subPacketSetting = SPS_200_00;
// 	configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_ENABLED;
// 	configuration.OPTION.transmissionPower = POWER_10;

// 	configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
// 	configuration.TRANSMISSION_MODE.fixedTransmission = 0;
// 	configuration.TRANSMISSION_MODE.enableLBT = LBT_ENABLED;
// 	configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;

// 	ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
// 	Serial.println(rs.getResponseDescription());
// 	Serial.println(rs.code);
// }

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

// void LORA_activate(bool act)
// {
// 	if (act)
// 	{
// 		e220ttl.begin();
// 	}
// 	else
// 	{
// 		Serial1.end();
// 	}
// 	Serial.print("lora state	");
// 	Serial.println(act);
// 	delay(500);
// }

// bool LORA_messedge_send(byte datatosend[], int lendatatosend)
// {
// 	LORA_activate(true);
// 	ResponseStatus s;
// 	s = e220ttl.sendMessage(datatosend, lendatatosend);
// 	Serial.println(s.code);
// 	Serial.println(s.getResponseDescription());
// 	LORA_activate(false);
// }

// bool LORA_messedge_get()
// {
// 	Serial.println("wait mes");
// 	for (int i = 0; i < 40; i++)
// 	{
// 		Serial.println(i);
// 		delay(200);
// 		if (e220ttl.available() > 1)
// 		{
// 			ResponseContainer rc = e220ttl.receiveMessage();
// 			if (rc.status.code != 1)
// 			{
// 				Serial.println(rc.status.getResponseDescription());
// 			}
// 			else
// 			{
// 				Serial.println(rc.status.getResponseDescription());
// 				byte newID[3];
// 				byte myID[3] = {};
// 				int length = rc.data.length();
// 				myID[0] = (byte)(ID >> 16) & 0xFF;
// 				myID[1] = (byte)(ID >> 8) & 0xFF;
// 				myID[2] = (byte)(ID) & 0xFF;
// 				// Преобразуем строку в массив байтов
// 				for (int i = 0; i < 3; i++)
// 				{
// 					newID[i] = (byte)rc.data.charAt(i);
// 				}
// 				printHEX(newID, 3);

// 				if (myID[3] == newID[3])
// 				{
// 					Serial.println(" OK Recive");
// 					return true;
// 				}
// 			}
// 		}
// 	}
// 	return false;
// }

// bool LORA_messedge_check()
// {
// 	byte pak[3] = {};
// 	pak[0] = (byte)(ID >> 16) & 0xFF;
// 	pak[1] = (byte)(ID >> 8) & 0xFF;
// 	pak[2] = (byte)(ID) & 0xFF;
// 	pak[3] = 0x22;
// 	LORA_activate(true);
// 	ResponseStatus s;
// 	s = e220ttl.sendMessage(pak, 4);
// 	Serial.println(s.code);
// 	Serial.println(s.getResponseDescription());
// 	if (LORA_messedge_get())
// 	{
// 		Serial.println("OK");
// 		return true;
// 	}
// 	else
// 	{
// 		Serial.println("not OK");
// 		return false;
// 	}
// }

void port_activate(bool act)
{
}

// void sender()
// {
// 	LORA_activate(true);
// 	delay(200);

// 	ResponseStatus s;
// 	byte combinedData[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x11};
// 	for (int i = 0; i < 10; i++)
// 	{
// 		s = e220ttl.sendMessage(combinedData, 10);
// 		Serial.println(s.code);
// 		Serial.println(s.getResponseDescription());
// 		delay(5000);
// 	}
// 	delay(5000);
// }

// void action()
// {
// }

// void sleep(int time)
// {
// 	esp_sleep_enable_timer_wakeup(time * uS_TO_S_FACTOR);
// 	Serial.print("Переход в глубокий сон...на ");
// 	Serial.println(time);
// 	delay(500);
// 	esp_deep_sleep_start();
// }

// void SIM_activate(bool act)
// {
// 	if (act)
// 	{
// 		SerialAT.begin(115200, SERIAL_8N1, 17, 16); // Adjust baud rate and pins as needed
// 		Serial.println("SIM activate");
// 	}
// 	else
// 	{
// 		SerialAT.end(); // Adjust baud rate and pins as needed
// 		Serial.println("SIM DEactivate");
// 	}
// 	delay(500);
// }

// void connect()
// {

// 	Serial.print(F("Modem Info: "));
// 	String modemInfo = modem.getModemRevision();
// 	String modemModel = modem.getModemManufacturer();

// 	Serial.println(modemInfo);
// 	Serial.println(modemModel);

// 	// Wait for network registration
// 	Serial.print(F("Waiting for network..."));
// 	if (!modem.waitForNetwork())
// 	{
// 		Serial.println(F(" Failed to connect to network!"));
// 		return;
// 	}
// 	Serial.println(F(" Network connected!"));

// 	// Establish GPRS connection
// 	Serial.print(F("Connecting to GPRS..."));
// 	if (!modem.gprsConnect(apn, gprsUser, gprsPass))
// 	{
// 		Serial.println(F(" Failed to connect to GPRS!"));
// 		return;
// 	}
// 	Serial.println(F(" GPRS connected!"));

// 	int year, month, day, hour, minute, second;

// 	modem.getGsmLocationTime(&year, &month, &day, &hour, &minute, &second);

// 	Serial.println(year);
// 	Serial.println(month);
// 	Serial.println(day);
// 	Serial.println(hour);
// 	Serial.println(minute);
// 	Serial.println(second);
// }

void sendRS485Data(byte *data, int len)
{
	digitalWrite(REDE, HIGH);
	delay(200);
	rs485Serial.write(data, len);
	rs485Serial.flush();
	digitalWrite(REDE, LOW);
}

bool getSOILdata()
{

	byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};

	int lenreq = 8;
	int lenresponse = 32;
	byte response[lenresponse] = {};

	printHEX(req, lenreq);
	sendRS485Data(req, lenreq);
	delay(500);
	rs485Serial.readBytes(response, lenresponse);
	rs485Serial.flush();

	Serial.println("	response:");
	printHEX(response, lenresponse);
}
void en_12v(bool act)
{
	if (act)
	{
		// pinMode(E12V, OUTPUT);
		digitalWrite(E12V, HIGH);
		delay(200);
		Serial.println("12v ON");
	}
	else
	{
		digitalWrite(E12V, LOW);
		delay(200);
		Serial.println("12v OFF");
	}
}
void en_5v(bool act)
{
	if (act)
	{
		// pinMode(E12V, OUTPUT);
		digitalWrite(E5V, HIGH);
		delay(200);
		Serial.println("5v ON");
	}
	else
	{
		digitalWrite(E5V, LOW);
		delay(200);
		Serial.println("5v OFF");
	}
}
void enable_sens(int port)
{

	switch (port)
	{
	case 1:
		// pinMode(EG1, OUTPUT);
		digitalWrite(EG1, HIGH);
		Serial.println("port 1 ON");
		break;
	case 2:
		// pinMode(EG2, OUTPUT);
		digitalWrite(EG2, HIGH);
		Serial.println("port 2 ON");
		break;
	case 3:
		// pinMode(EG3, OUTPUT);
		digitalWrite(EG3, HIGH);
		Serial.println("port 3 ON");
		break;
	case 4:
		// pinMode(EG4, OUTPUT);
		digitalWrite(EG4, HIGH);
		Serial.println("port 4 ON");
		break;
	default:
		digitalWrite(EG1, LOW);
		digitalWrite(EG2, LOW);
		digitalWrite(EG3, LOW);
		digitalWrite(EG4, LOW);
		Serial.println("port 1234 OFF");
		break;
	}

	delay(500);
}

void setup()
{
	pinMode(LED_PIN, OUTPUT);
	pinMode(EG1, OUTPUT);
	pinMode(EG2, OUTPUT);
	pinMode(EG3, OUTPUT);
	pinMode(EG4, OUTPUT);
	pinMode(E12V, OUTPUT);
	pinMode(E5V, OUTPUT);
	pinMode(REDE, OUTPUT);

	Serial.begin(115200); // монитор порта
	for (int i = 0; i < 40; i++)
	{
		if (!Serial)
		{
			blink(1, 200);
		}
	}
	// while (!Serial)
	// {
	// }
	delay(200);
	Serial.println("hello"); // Startup all pins and UART

	// port_activate(6, true);
	// port_activate(9, true);

	Serial.println("hello"); // Startup all pins and UART
							 // SIM_activate(true);
							 // delay(200);
							 // // SerialAT.begin(115200, SERIAL_8N1, 39, 40); // Adjust baud rate and pins as needed
	en_12v(true);
	en_5v(true);
	enable_sens(1);
	enable_sens(3);
	delay(500);
	RS485_2_activate(true);
	// delay(200);
	// connect();

	// action();
	// Serial.println("sleep");
	// sleep(15);
}

void loop()
{
	  byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};
	// byte req[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xff};
	// Отправка
	//   digitalWrite(REDE, HIGH);
	//   rs485Serial.write(req, sizeof(req));
	//   rs485Serial.flush();
	//   digitalWrite(REDE, LOW);
	sendRS485Data(req, 8);
	Serial.println("sended");
		// Прием
		delay(1000);
	if (rs485Serial.available())
	{
		Serial.print("Ответ: ");
		while (rs485Serial.available())
		{
			Serial.printf("%02X ", rs485Serial.read());
		}
		Serial.println();
	}
	delay(2000);
}

// void loop()
// {

// 	RS485_1_activate(true);
// 	delay(500);
// 	getSOILdata();
// 	RS485_1_activate(false);
// 	delay(500);

// 	RS485_2_activate(true);
// 	delay(500);
// 	getSOILdata();
// 	delay(500);
// 	RS485_2_activate(false);
// }

// // Connect to the server
// Serial.print(F("Connecting to "));
// Serial.print(server);
// Serial.print(F(":"));
// Serial.println(port);

// if (!client.connect(server, port))
// {
// 	Serial.println(F(" Failed to connect to server!"));
// 	delay(2000);
// 	modem.restart();
// 	connect();
// 	return;
// }
// Serial.println(F(" Connected to server!"));

// // Send data (e.g., an HTTP GET request)
// client.println("GET / HTTP/1.1");
// client.print("Host: ");
// client.println(server);
// client.println("Connection: close");
// client.println();

// unsigned long timeout = millis();
// while (client.connected() && millis() - timeout < 10000L)
// {
// 	while (client.available())
// 	{
// 		char c = client.read();
// 		Serial.print(c);
// 		timeout = millis();
// 	}
// }

// // Close the client connection and disconnect GPRS
// client.stop();
// Serial.println(F("Server disconnected"));
// modem.gprsDisconnect();
// Serial.println(F("GPRS disconnected"));

// delay(30000); // Wait for 30 seconds before repeating

// void loop()
// {
// 	// Serial.println("wait");
// 	if (e220ttl.available() > 1)
// 	{
// 		digitalWrite(LED_PIN, HIGH);
// 		ResponseContainer rc = e220ttl.receiveMessage();
// 		Serial.println(rc.status.getResponseDescription());
// 		int length = rc.data.length();
// 		byte data[length];
// 		for (int i = 0; i < length; i++)
// 		{
// 			data[i] = (byte)rc.data.charAt(i);
// 		}
// 		printHEX(data, length);

// 		digitalWrite(LED_PIN, LOW);
// 	}
// 	delay(1);
// }

// void loop()
// {
// 	for (byte i = 0x00; i < 0xff; i++)
// 	{
// 		digitalWrite(LED_PIN, HIGH);
// 		byte id=0x00;
// 		// byte datatosend[176] = {
// 			// 0x66, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i};
// 		byte datatosend[30] = {
// 			id, i,i,i,i,id, i,i,i,i,id, i,i,i,i,id, i,i,i,i,id, i,i,i,i,id, i,i,i,i};
// 		ResponseStatus s;
// 		s = e220ttl.sendMessage(datatosend, 30);
// 		Serial.println(s.code);
// 		Serial.println(s.getResponseDescription());
// 		if (s.getResponseDescription() == "Success")
// 		{
// 			delay(500);
// 			digitalWrite(LED_PIN, LOW);
// 		}
// 		delay(500);
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