#include <sys.h>
esp_adc_cal_characteristics_t adc_chars;
Preferences preferences;
void initPins()
{
	pinMode(LED_PIN, OUTPUT);
	pinMode(EG1, OUTPUT);
	pinMode(EG2, OUTPUT);
	pinMode(EG3, OUTPUT);
	pinMode(EG4, OUTPUT);
	pinMode(EP, OUTPUT);
	pinMode(ESIM, OUTPUT);

	// настройка для измерения батареи
	pinMode(ADC, INPUT);
	analogReadResolution(13);
	analogSetAttenuation(ADC_6db);
	esp_adc_cal_characterize(
		ADC_UNIT_1,
		ADC_ATTEN_DB_6,
		ADC_WIDTH_BIT_13,
		3300,
		&adc_chars);
}


void sleep(int time)
{
	esp_sleep_enable_timer_wakeup(time * uS_TO_S_FACTOR);
	Serial.print("Переход в глубокий сон...на ");
	Serial.println(time);
	delay(500);
	esp_deep_sleep_start();
}


int readBatteryVoltage()
{
	uint32_t raw = analogReadRaw(ADC);
	// Линейная интерполяция: Vbat = k * raw + b
	float k = (CAL_HIGH.vbat - CAL_LOW.vbat) / (CAL_HIGH.raw - CAL_LOW.raw);
	float b = CAL_LOW.vbat - k * CAL_LOW.raw;

	float v = k * raw + b;
	int vbat = v * 10;
	Serial.printf("RAW:%i, BAT: %.3f V, int:%i \n", raw, v, vbat);
	return vbat;
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

void printHEX(byte data[], int len)
{
	if (len==0){
		Serial.println("0xNONE");
	}
	for (int j = 0; j < len; j++)
	{
		Serial.print("0x");
		if (data[j] < 0x10)
			Serial.print("0"); // Добавляем ведущий ноль для однозначных HEX
		Serial.print(data[j], HEX);
		if (j != len - 1)
			Serial.print(", ");
	}
	Serial.println();
}

void saveArrayToFlash(byte data[])
{
	byte t[5] = {0x00, 0x24, 0x00, 0x02, 0x00};
	preferences.begin("my-data", false); // Открываем пространство имен "my-data"
	preferences.putBytes("array", data, 8);

	preferences.end();
	Serial.print("tableSens:		");
	printHEX(data, 5);
}

bool loadArrayFromFlash(byte data[])
{
	preferences.begin("my-data", true); // Открыть в режиме чтения
	size_t len = preferences.getBytes("array", data, 8);
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

void enable_power(bool act)
{
	if (act)
	{
		// pinMode(E12V, OUTPUT);
		digitalWrite(EP, HIGH);
		delay(200);
		Serial.println("5v ON");
	}
	else
	{
		digitalWrite(EP, LOW);
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
void enable_sim(bool act)
{
	if (act)
	{
		digitalWrite(ESIM, HIGH);
		delay(200);
		Serial.println("SIM ON");
	}
	else
	{
		digitalWrite(ESIM, LOW);
		delay(200);
		Serial.println("SIM OFF");
	}
}

void addCRC(byte req[], int dataLength, byte response[])
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
		response[i] = req[i];
	}

	// Добавляем CRC в конец (младший байт первый)
	response[dataLength] = crc & 0xFF;			  // LSB
	response[dataLength + 1] = (crc >> 8) & 0xFF; // MSB
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