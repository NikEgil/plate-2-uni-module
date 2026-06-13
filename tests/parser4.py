import json

# -------------------------------------------------------------
# CRC8 и checksum для WH65LP
# -------------------------------------------------------------
def crc8_wh65lp(data: bytes) -> int:
    crc = 0x00
    poly = 0x31
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ poly
            else:
                crc <<= 1
            crc &= 0xFF
    return crc

def checksum8(data: bytes) -> int:
    return sum(data) & 0xFF

def parse_wh65lp(raw: bytes, verify_crc: bool = True) -> dict:
    """Декодирование 25 байт метеостанции WH65LP."""
    if len(raw) < 25:
        return {"error": f"Expected 25 bytes, got {len(raw)}"}
    d = raw

    crc_ok = (crc8_wh65lp(d[0:15]) == d[15]) if verify_crc else None
    checksum_ok = (checksum8(d[0:16]) == d[16]) if verify_crc else None

    family_code = d[0]
    security_code = d[1]

    # Направление ветра (9 бит)
    dir_m = (d[2] >> 4) & 0x0F
    dir_l = d[2] & 0x0F
    dir_h = (d[3] >> 4) & 0x0F
    dir_8 = (dir_h >> 3) & 0x01
    wind_direction = (dir_8 << 8) | (dir_m << 4) | dir_l
    if wind_direction == 0x1FF:
        wind_direction = None

    wsp_flag = (dir_h >> 2) & 0x01
    wsp_9 = (dir_h >> 1) & 0x01
    wsp_8 = dir_h & 0x01

    # Температура (11 бит) + low battery
    tmp_h_nib = d[3] & 0x0F
    low_battery = (tmp_h_nib >> 3) & 0x01
    tmp_10 = (tmp_h_nib >> 2) & 0x01
    tmp_9 = (tmp_h_nib >> 1) & 0x01
    tmp_8 = tmp_h_nib & 0x01
    tmp_m = (d[4] >> 4) & 0x0F
    tmp_l = d[4] & 0x0F
    temp_raw = (tmp_10 << 10) | (tmp_9 << 9) | (tmp_8 << 8) | (tmp_m << 4) | tmp_l
    temperature = None if temp_raw == 0x7FF else (temp_raw - 400) / 10.0

    humidity = d[5] if d[5] != 0xFF else None

    # Скорость ветра
    wsp_h = (d[6] >> 4) & 0x0F
    wsp_l = d[6] & 0x0F
    if wsp_flag == 0:
        wsp_raw = (wsp_9 << 9) | (wsp_8 << 8) | (wsp_h << 4) | wsp_l
    else:
        wsp_raw = (wsp_8 << 8) | (wsp_h << 4) | wsp_l
    wind_speed = None if wsp_raw == 0x1FF else (wsp_raw / 8.0) * 0.51

    # Порывы
    gust_raw = d[7]
    gust = None if gust_raw == 0xFF else gust_raw * 0.51

    # Дождь
    rain_raw = (d[8] << 8) | d[9]
    rainfall = rain_raw * 0.254

    # УФ
    uv_raw = (d[10] << 8) | d[11]
    uv = None if uv_raw == 0xFFFF else uv_raw

    # Освещённость
    light_raw = (d[12] << 16) | (d[13] << 8) | d[14]
    illumination = None if light_raw == 0xFFFFFF else light_raw / 10.0

    # Давление (байты 17,18,19)
    pressure_raw = (d[17] << 16) | (d[18] << 8) | d[19]
    pressure = None if pressure_raw == 0xFFFFFF else pressure_raw / 100.0

    result = {
        "family_code": family_code,
        "security_code": security_code,
        "low_battery": bool(low_battery),
        "air_temperature": temperature,
        "air_humidity": humidity,
        "air_pressure": round(pressure, 2) if pressure is not None else None,
        "wind_direction": wind_direction,
        "wind_speed": round(wind_speed, 2) if wind_speed is not None else None,
        "wind_gust": round(gust, 2) if gust is not None else None,
        "rainfall": round(rainfall, 2),
        "uv": uv,
        "illumination": round(illumination, 1) if illumination is not None else None,
    }
    if verify_crc:
        result["crc8_valid"] = crc_ok
        result["checksum_valid"] = checksum_ok
    return result

# -------------------------------------------------------------
# Общие функции
# -------------------------------------------------------------
def calc_crc16(data: bytes) -> int:
    """CRC16 Modbus (полином 0xA001). Возвращает число в big-endian представлении."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF

def voltage_to_percent(voltage: float) -> int:
    if voltage >= 4.2:
        return 100
    if voltage <= 2.7:
        return 0
    return int(round((voltage - 2.7) / (4.2 - 2.7) * 100))

def load_config(path="sensor_config.json") -> dict:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return {"field_mapping": {}}

def decode_sensor(data: bytes, sid: int, cfg: dict) -> dict:
    res = {"id_sens": sid, "raw": data.hex().upper(), "decoded": {}}
    if sid == 36:
        weather = parse_wh65lp(data, verify_crc=True)
        for name, value in weather.items():
            if value is None:
                res["decoded"][name] = None
            else:
                res["decoded"][name] = value
        return res
    fmap = cfg.get("field_mapping", {}).get(str(sid), [])
    if not fmap:
        res["decoded"] = {"raw": data.hex(), "note": "нет конфигурации"}
        return res
    for f in fmap:
        off = (f["byte"] - 1) * 2
        if off + 2 <= len(data):
            raw_val = int.from_bytes(data[off:off+2], "big")
            value = raw_val * f["coef"]
            rounded = round(value, 1)
            res["decoded"][f["name"]] = {
                "raw": raw_val,
                "coef": f["coef"],
                "value": rounded,
            }
    return res

# -------------------------------------------------------------
# Служебные пакеты (дополнение)
# -------------------------------------------------------------

def decode_service_field_packet(hex_str: str) -> str:
    """Декодирование служебного пакета от полевого модуля (encode_to_buffer).
    
    Формат: LEN(1) + ID_field(3) + 0xFF 0xFF 0xFF + time(6) + message(N)
    """
    hex_str = hex_str.replace(" ", "")
    data = bytes.fromhex(hex_str)

    pkt_len = data[0]
    mod_id = int.from_bytes(data[1:4], "big")
    ff_marker = data[4:7]
    if ff_marker != b'\xff\xff\xff':
        return json.dumps({"error": "Not a service field packet (missing FF FF FF marker)"}, ensure_ascii=False)

    year = data[7]
    month = data[8]
    day = data[9]
    hour = data[10]
    minute = data[11]
    second = data[12]
    date_str = f"20{year:02d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}"

    # Остальное — текст ASCII
    msg_bytes = data[13:pkt_len]
    message = msg_bytes.decode("ascii", errors="replace")

    result = {
        "packet_type": "service_field",
        "pkt_len": pkt_len,
        "field_id": f"{mod_id:08d}",
        "time": date_str,
        "message": message,
    }
    return json.dumps(result, indent=2, ensure_ascii=False)


def decode_head_only_packet(hex_str: str, server_time: str = None) -> str:
    """Декодирование заголовочного пакета от головного модуля (adding без сенсорных данных).
    
    Формат: ccid(10) + ID_gm(3) + bat(1) + signal(1) + rssi(1) = 16 байт
    """
    hex_str = hex_str.replace(" ", "")
    data = bytes.fromhex(hex_str)

    if len(data) < 16:
        return json.dumps({"error": f"Header-only packet too short: {len(data)} bytes"}, ensure_ascii=False)

    ccid = data[0:10].hex().upper()
    module_id = int.from_bytes(data[10:13], "big")
    bat_head_voltage = data[13] / 10.0
    signal = data[14]
    rssi = data[15]

    head_sens = {
        "ccid": ccid,
        "id_s": f"{module_id:08d}",
        "batt": voltage_to_percent(bat_head_voltage),
        "signal": signal,
        "rssi": rssi,
    }
    if server_time:
        head_sens["server_received_at"] = server_time

    result = {
        "packet_type": "head_only",
        "head_sens": head_sens,
    }
    return json.dumps(result, indent=2, ensure_ascii=False)


# -------------------------------------------------------------
# Основная функция декодирования пакета
# -------------------------------------------------------------
def decode_hex_packet(
    hex_str: str, server_time: str = None, config_path: str = "sensor_config.json"
) -> str:
    config = load_config(config_path)
    hex_str = hex_str.replace(" ", "")
    data = bytes.fromhex(hex_str)
    off = 0

    # ---------- HEADER (16 байт) ----------
    ccid = data[0:10].hex().upper()
    module_id = int.from_bytes(data[10:13], "big")
    bat_head_voltage = data[13] / 10.0
    signal = data[14]
    rssi = data[15]
    off = 16
    block_start = off   # начало блока для CRC (байт pkt_len)

    # ---------- Детекция служебных пакетов ----------

    # Служебный пакет от полевого модуля: после заголовка идёт FF FF FF
    # (encode_to_buffer: LEN + ID_field + 0xFF 0xFF 0xFF + time + message)
    if off + 4 <= len(data) and data[off+1:off+4] == b'\xff\xff\xff':
        # Заголовок ГМ отсутствует — значит это пакет без заголовка
        # (полевой модуль сам отправляет encode_to_buffer через LoRa)
        # В этом случае ccid/module_id/signal/rssi = 0, а данные начинаются с off
        return decode_service_field_packet(hex_str)

    # Если заголовок есть (ccid != 0) и дальше идёт pkt_len с FF FF FF
    # — значит adding() обернул encode_to_buffer
    if off + 4 <= len(data) and data[off+4:off+7] == b'\xff\xff\xff':
        # Это adding() + encode_to_buffer: header(16) + service_field_payload
        # Декодируем заголовок ГМ + вложенный служебный пакет
        service_data = data[off:]
        service_json = json.loads(decode_service_field_packet(service_data.hex()))
        head_sens = {
            "ccid": ccid,
            "id_s": f"{module_id:08d}",
            "batt": voltage_to_percent(bat_head_voltage),
            "signal": signal,
        }
        if rssi != 0:
            head_sens["rssi"] = rssi
        if server_time:
            head_sens["server_received_at"] = server_time
        result = {
            "packet_type": "service_message",
            "head_sens": head_sens,
            "field_sens": service_json,
        }
        return json.dumps(result, indent=2, ensure_ascii=False)

    # Заголовок ГМ без данных сенсоров: ровно 16 байт
    if len(data) == 16:
        return decode_head_only_packet(hex_str, server_time)

    # ---------- Обычный пакет (существующий алгоритм) ----------

    # ---------- FIELD ----------
    pkt_len = data[off]
    off += 1
    mod_id = int.from_bytes(data[off:off+3], "big")
    off += 3
    bat_mem_voltage = data[off] / 10.0
    off += 1
    year = data[off]
    off += 1
    month = data[off]
    off += 1
    day = data[off]
    off += 1
    hour = data[off]
    off += 1
    minute = data[off]
    off += 1
    second = data[off]
    off += 1
    date_str = f"20{year:02d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}"

    head_sens = {
        "ccid": ccid,
        "id_s": f"{module_id:08d}",
        "batt": voltage_to_percent(bat_head_voltage),
        "signal": signal,
    }
    if server_time:
        head_sens["server_received_at"] = server_time

    field_sens = {
        "id_s": f"{mod_id:08d}",
        "batt": voltage_to_percent(bat_mem_voltage),
        "time": date_str,
    }
    if rssi != 0:
        field_sens["signal"] = rssi

    result = {"head_sens": head_sens, "field_sens": field_sens, "ports": {}}

    # ---------- Порты ----------
    while off < len(data) - 2:
        port = data[off]
        off += 1
        if port == 0:
            break

        address = data[off]
        off += 1

        # ----- Метеостанция (address=36) -----
        if address == 36:
            sdata = data[off:off+24]
            off += 24
            full_data = bytes([address]) + sdata
            decoded = decode_sensor(full_data, address, config)
            sensor_item = {"address": address}
            for name, val in decoded.get("decoded", {}).items():
                if isinstance(val, dict) and "value" in val:
                    sensor_item[name] = val["value"]
                else:
                    sensor_item[name] = val
            port_key = f"port_{port}"
            result["ports"].setdefault(port_key, []).append(sensor_item)
            continue

        # ----- Прочие датчики -----
        op = data[off]
        off += 1
        dlen = data[off]
        off += 1
        sdata = data[off:off+dlen]
        off += dlen
        scrc = int.from_bytes(data[off:off+2], "big")
        off += 2

        decoded = decode_sensor(sdata, address, config)
        sensor_item = {"address": address}
        for name, val in decoded.get("decoded", {}).items():
            if isinstance(val, dict) and "value" in val:
                sensor_item[name] = val["value"]
            else:
                sensor_item[name] = val
        port_key = f"port_{port}"
        result["ports"].setdefault(port_key, []).append(sensor_item)

    # ---------- Итоговая CRC ----------
    total_block = data[block_start:off]
    total_crc = int.from_bytes(data[off:off+2], "little")
    computed_crc = calc_crc16(total_block)
    result["crc_total"] = total_crc
    result["crc_total_valid"] = (computed_crc == total_crc)

    return json.dumps(result, indent=2, ensure_ascii=False)


print(decode_hex_packet("8970101829255646914600001911430014000019ffffff1a060b07082c77616b65207570"))
