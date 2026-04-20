#include <Arduino.h>
#include <HardwareSerial.h>
#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_DEBUG Serial

#include <TinyGsmClient.h>
#include <StreamDebugger.h>
#include <PubSubClient.h>
HardwareSerial SerialAT(1); // Using Serial1 on ESP32
// StreamDebugger debugger(SerialAT, Serial); // Пересылает всё в Serial

#include <defenitions.h>
#include <sys.h>
#include <rs.h>
// Структура для хранения калибровочных данных

float Battery = 146;

byte tableSens[5] = {};

// #define rs485Serial Serial1
// LoRa_E220 e220ttl(&MySerial, 15, 21, 19); //  RXTX AUX M0 M1
String readRS485Response();

// Modem power pin (if applicable, adjust for your board)

// Create a TinyGSM modem instance
TinyGsm modem(SerialAT);
// Create a TinyGSM client instance for network communication
TinyGsmClient client(modem);
PubSubClient mqtt(client);

// #define MySerial Serial2
// HardwareSerial rs485Serial(1);

// Serial1.begin(115200, SERIAL_8N1, 16, 17);
// LoRa_E220 e220ttl(&Serial1, 8, 18, 21); //  RX TX AUX M0 M1

// LoRa_E220 e220ttl(&Serial1, 8, 18, 21); //  RX TX AUX M0 M1

// void printParameters(struct Configuration configuration)
// {
// 	Serial.println("----------------------------------------");
// 	Serial.print(F("HEAD : "));
// 	Serial.print(configuration.COMMAND, HEX);
// 	Serial.print(" ");
// 	Serial.print(configuration.STARTING_ADDRESS, HEX);
// 	Serial.print(" ");
// 	Serial.println(configuration.LENGHT, HEX);
// 	Serial.println(F(" "));
// 	Serial.print(F("AddH : "));
// 	Serial.println(configuration.ADDH, HEX);
// 	Serial.print(F("AddL : "));
// 	Serial.println(configuration.ADDL, HEX);
// 	Serial.println(F(" "));
// 	Serial.print(F("Chan : "));
// 	Serial.print(configuration.CHAN, DEC);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.getChannelDescription());
// 	Serial.println(F(" "));
// 	Serial.print(F("SpeedParityBit     : "));
// 	Serial.print(configuration.SPED.uartParity, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.SPED.getUARTParityDescription());
// 	Serial.print(F("SpeedUARTDatte     : "));
// 	Serial.print(configuration.SPED.uartBaudRate, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.SPED.getUARTBaudRateDescription());
// 	Serial.print(F("SpeedAirDataRate   : "));
// 	Serial.print(configuration.SPED.airDataRate, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.SPED.getAirDataRateDescription());
// 	Serial.println(F(" "));
// 	Serial.print(F("OptionSubPacketSett: "));
// 	Serial.print(configuration.OPTION.subPacketSetting, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.OPTION.getSubPacketSetting());
// 	Serial.print(F("OptionTranPower    : "));
// 	Serial.print(configuration.OPTION.transmissionPower, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.OPTION.getTransmissionPowerDescription());
// 	Serial.print(F("OptionRSSIAmbientNo: "));
// 	Serial.print(configuration.OPTION.RSSIAmbientNoise, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.OPTION.getRSSIAmbientNoiseEnable());
// 	Serial.println(F(" "));
// 	Serial.print(F("TransModeWORPeriod : "));
// 	Serial.print(configuration.TRANSMISSION_MODE.WORPeriod, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.TRANSMISSION_MODE.getWORPeriodByParamsDescription());
// 	Serial.print(F("TransModeEnableLBT : "));
// 	Serial.print(configuration.TRANSMISSION_MODE.enableLBT, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.TRANSMISSION_MODE.getLBTEnableByteDescription());
// 	Serial.print(F("TransModeEnableRSSI: "));
// 	Serial.print(configuration.TRANSMISSION_MODE.enableRSSI, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.TRANSMISSION_MODE.getRSSIEnableByteDescription());
// 	Serial.print(F("TransModeFixedTrans: "));
// 	Serial.print(configuration.TRANSMISSION_MODE.fixedTransmission, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.TRANSMISSION_MODE.getFixedTransmissionDescription());
// 	Serial.println("----------------------------------------");
// }
// void printModuleInformation(struct ModuleInformation moduleInformation)
// {
// 	Serial.println("----------------------------------------");
// 	Serial.print(F("HEAD: "));
// 	Serial.print(moduleInformation.COMMAND, HEX);
// 	Serial.print(" ");
// 	Serial.print(moduleInformation.STARTING_ADDRESS, HEX);
// 	Serial.print(" ");
// 	Serial.println(moduleInformation.LENGHT, DEC);
// 	Serial.print(F("Model no.: "));
// 	Serial.println(moduleInformation.model, HEX);
// 	Serial.print(F("Version  : "));
// 	Serial.println(moduleInformation.version, HEX);
// 	Serial.print(F("Features : "));
// 	Serial.println(moduleInformation.features, HEX);
// 	Serial.println("----------------------------------------");
// }

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

// bool getSOILdata()
// {
// 	byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};
// 	int lenreq = 8;
// 	int lenresponse = 32;
// 	byte response[lenresponse] = {};
// 	printHEX(req, lenreq);
// 	sendRS485Data(req, lenreq);
// 	delay(500);
// 	rs485Serial.readBytes(response, lenresponse);
// 	rs485Serial.flush();
// 	Serial.println("	response:");
// 	printHEX(response, lenresponse);
// 	return true;
// }

// void action()
// {
// }

void SIM_activate(bool act)
{
	if (act)
	{
		SerialAT.begin(115200, SERIAL_8N1, SIMRX, SIMTX, false); // Adjust baud rate and pins as needed
		Serial.println("SIM activate");
	}
	else
	{
		SerialAT.end(); // Adjust baud rate and pins as needed
		Serial.println("SIM DEactivate");
	}
	delay(200);
}

bool connect()
{
	Serial.print(F("Modem Info: "));
	Serial.println(modem.getModemModel());
	// Serial.println(modem.getIMEI());
	// modem.sendAT("E0");
	// Serial.println(modem.waitResponse());
	// Wait for network registration
	// delay(100);
	// int i = ;
	delay(5000);
	// Serial.println(modem.getIMEI());
	// Serial.println(modem.factoryDefault());

	Serial.println(F("Waiting for network..."));
	for (int i = 1; i < 4; i++)
	{
		Serial.print("try ");
		Serial.print(i);
		if (modem.waitForNetwork(30000U, true))
		{
			Serial.println(F(" Network connected!"));
			break;
		}
		else
		{
			Serial.println(F(" Failed to connect to network!"));
			// if (modem.restart())
			// {
			// 	Serial.println("restarted");
			// }
			// else
			// {
			// 	Serial.println("can't restarted");
			// }
			delay(200);
			if (i == 3)
			{
				return false;
			}
		}
	}

	delay(5000); // Дайте модему время получить время от сети

	// int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0;
	// if (modem.getGsmLocationTime(&y, &m, &d, &h, &min, &s))
	// {
	// 	Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", y, m, d, h, min, s);
	// }
	// else
	// {
	// 	Serial.println("Время не получено");
	// }

	// Establish GPRS connection
	Serial.println("Connecting to GPRS...");
	// if (!modem.gprsConnect(apn, gprsUser, gprsPass))
	// {
	// 	Serial.println(F(" Failed to connect to GPRS!"));
	// 	return false;
	// }

	for (int i = 1; i < 4; i++)
	{
		Serial.print("try ");
		Serial.print(i);
		if (modem.gprsConnect(apn, gprsUser, gprsPass))
		{
			Serial.println(F(" GPRS connected!"));
			break;
		}
		else
		{
			Serial.println(F(" Failed to connect to GPRS!"));
			// if (modem.restart())
			// {
			// 	Serial.println("restarted");
			// }
			// else
			// {
			// 	Serial.println("can't restarted");
			// }
			delay(200);
			if (i == 3)
			{
				return false;
			}
		}
	}
	Serial.print("signal:	");
	Serial.println(modem.getSignalQuality());
	Serial.print("ip:	");
	Serial.println(modem.getLocalIP());
	delay(10000);
	return true;
}

bool httpRequest(const char *host, uint16_t port, const char *path)
{
	Serial.printf("\n=== HTTP Request ===\n");
	Serial.printf("Target: %s:%d%s\n", host, port, path);

	// 1. Подключение
	if (!client.connect(host, port))
	{
		Serial.println("❌ Connection failed");
		return false;
	}
	Serial.println("✅ Connected");

	// 2. Отправка запроса
	client.print(String("GET ") + path + " HTTP/1.1\r\n");
	client.print(String("Host: ") + host + "\r\n");
	client.println("Connection: close\r\n");
	Serial.println("📤 Request sent");

	// 3. Ждём начала ответа (таймаут 3 сек)
	unsigned long start = millis();
	while (!client.available() && millis() - start < 3000)
	{
		delay(10);
	}

	if (!client.available())
	{
		Serial.println("❌ No response received");
		client.stop();
		return false;
	}

	// 4. Чтение и вывод ответа
	Serial.println("\n📥 Response:");
	Serial.println("---[BEGIN]---");

	int totalBytes = 0;
	while (client.connected() || client.available())
	{
		if (client.available())
		{
			String line = client.readStringUntil('\n');
			Serial.print(line); // \n уже есть в строке
			totalBytes += line.length();

			// Подсветка статуса
			if (line.indexOf("200 OK") >= 0)
			{
				Serial.println("\n✓ HTTP 200 OK detected");
			}
		}
		delay(5); // даём буферу наполниться
	}

	Serial.println("---[END]---");
	Serial.printf("Total received: %d bytes\n", totalBytes);

	client.stop();
	return true;
}

// bool searche_multisens()
// {
// 	Serial.println("founding multisens");
// 	byte req1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
// 	int lenresponse1 = 7;
// 	byte response1[lenresponse1] = {};
// 	sendRS485Data(req1, 8);
// 	delay(500);
// 	rs485Serial.readBytes(response1, lenresponse1);
// 	rs485Serial.flush();
// 	Serial.println("	request:");
// 	printHEX(req1, lenresponse1);
// 	Serial.println("	response:");
// 	printHEX(response1, lenresponse1);
// 	if (response1[0] == 0x01)
// 	{
// 		Serial.println("multisens is FOUND!!!");
// 		return true;
// 	}
// 	else
// 	{
// 		return false;
// 	}
// }

// void deact()
// {
// 	enable_sens(0);
// 	RS485_1_activate(false);
// 	RS485_2_activate(false);
// 	enable_power(false);
// 	// enable_5v(false);
// }

boolean mqttConnect()
{
	Serial.print("Connecting to ");
	Serial.print(broker);

	// Connect to MQTT Broker
	// char *id = "99999999";

	boolean status = mqtt.connect(IDchar, IDchar, pass);
	// Or, if you want to authenticate MQTT:
	// boolean status = mqtt.connect("GsmClientName", "mqtt_user", "mqtt_pass");

	if (status == false)
	{
		Serial.println(" fail");
		return false;
	}
	Serial.println(" success");

	return mqtt.connected();
}

void mqtt_send()
{
	char topic[64] = "mqtt/devices/";
	const char *topic_end = "/data";
	strcat(topic, IDchar);
	strcat(topic, topic_end);
	Serial.println(topic);
	const char *pay = "dadata";
	byte data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xff};
	bool s = mqtt.publish(topic, pay);
	s = mqtt.publish(topic, data, sizeof(data));
	if (s == true)
	{
		Serial.println("otpr");
	}
	else
	{
		Serial.println("ne otpr");
	}
	mqtt.disconnect();
}

bool searche_multisens()
{
	Serial.println("founding multisens");
	byte req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
	Serial.println("	request:");
	printHEX(req, sizeof(req));
	RsModbus::sendData(req, sizeof(req));

	delay(500);

	byte response[32] = {0};
	// Ждём ответ 150 мс (для Modbus обычно хватает 50-100 мс)
	size_t lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
	Serial.println("	responses:");
	printHEX(response, lenresponse);

	if (response[0] == 0x01)
	{
		Serial.println("multisens is FOUND!!!");
		return true;
	}
	else
	{
		return false;
	}
}

bool searchSensors(int port)
{
	pinMode(LED_PIN, HIGH);
	enable_power(true);
	Serial.print("			Search sensors, port	");
	Serial.println(port);
	enable_sens(port);
	delay(1000);

	if (port == 1)
	{
		RsModbus::setChannel(RsModbus::RS_CH1, true);
		Serial.println("Channel 1 activated");
	}
	else
	{
		RsModbus::setChannel(RsModbus::RS_CH2, true);
		Serial.println("Channel 2 activated");
	}
	delay(1000);

	if (searche_multisens())
	{
		tableSens[port] = 0x01;
		saveArrayToFlash(tableSens);
		blink(1, 1000);
		return true;
	}
	Serial.println("founding sens");

	Serial.println("	request:");
	byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};

	int lenreq = 8;
	printHEX(req, lenreq);
	RsModbus::sendData(req, sizeof(req));
	delay(500);

	byte response[32] = {0};
	// Ждём ответ 150 мс (для Modbus обычно хватает 50-100 мс)
	size_t lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
	Serial.println("	responses:");
	printHEX(response, lenresponse);
	Serial.println("	wait meteo:");
	if (response[0] == 0x00)
	{
		for (int i = 0; i < 40; i++)
		{
			delay(500);
			lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
			printHEX(response, lenresponse);
			if (response[0] != 0)
			{
				break;
			}
		}
	}

	Serial.print((int)response[0]);
	Serial.println("		FOUND		!!!");
	if (response[0] == 0x24)
	{
		Serial.println("meteostation detected");
	}
	else if (response[0] != 0x00)
	{
		Serial.println("another sens detected");
	}
	else
	{
		Serial.println("		NOT SENS	!!!");
	}

	tableSens[port] = response[0];
	saveArrayToFlash(tableSens);
	enable_sens(0);
	// RS485_activate(0);
	enable_power(0);

	if (response[0] != 0x00)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void setup()
{
	initPins();

	Serial.begin(115200); // монитор порта
	for (int i = 0; i < 40; i++)
	{
		if (!Serial)
		{
			blink(1, 100);
		}
	}
	RsModbus::init(REDE);
	// enable_power(true);
	// enable_sim(true);
	// enable_sens(1);
	// enable_sens(4);

	delay(2000);
	Serial.println("hello");
	Serial.println("hello");

	Serial.println(readBatteryVoltage());
	Serial.println(broker);
	blink(5, 200);

	// 0x91, 0x59
	// SIM_activate(true);

	// delay(200);
	// if (connect())
	// {
	// 	mqtt.setServer(broker, 1883);
	// 	if (mqttConnect())
	// 	{
	// 		Serial.println("podklucheno");
	// 		mqtt_send();
	// 	}
	// 	httpRequest(broker, 1883, "/");
	// }

	// action();
	// Serial.println("sleep");
	// sleep(15);
	// 	if (searchSensors_1())
	// 	{
	// 		Serial.println("da");
	// 	}
	// 	else
	// 	{
	// 		Serial.println("no");
	// 	}
}

void loop()
{
	searchSensors(1);
	delay(5000);
	searchSensors(4);
	delay(5000);

	// Serial.println(readBatteryVoltage());
	// blink(5, 200);

	// Serial.println("		выкл");
	// enable_power(false);
	// float v_bat = readBatteryVoltage(390000, 100000);
	// Serial.printf("Батарея: %.2f V\n", v_bat);
	// Serial.println(analogReadRaw(10));
	// delay(5000);

	// byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};
	// // byte req[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xff};
	// // Отправка
	// //   digitalWrite(REDE, HIGH);
	// //   rs485Serial.write(req, sizeof(req));
	// //   rs485Serial.flush();
	// //   digitalWrite(REDE, LOW);
	// sendRS485Data(req, 8);
	// Serial.println("sended");
	// // Прием
	// delay(1000);
	// if (rs485Serial.available())
	// {
	// 	Serial.print("Ответ: ");
	// 	while (rs485Serial.available())
	// 	{
	// 		Serial.printf("%02X ", rs485Serial.read());
	// 	}
	// 	Serial.println();
	// }
	// delay(2000);
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