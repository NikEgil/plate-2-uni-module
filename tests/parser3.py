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

    # Осадки
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
        "air_temperature": round(temperature, 1) if temperature is not None else None,
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
                res["decoded"][name] = {"value": None, "valid": False}
            elif isinstance(value, bool):
                res["decoded"][name] = {"value": value}
            else:
                res["decoded"][name] = {"value": value, "valid": True}
        return res

    fmap = cfg.get("field_mapping", {}).get(str(sid), [])
    if not fmap:
        res["decoded"] = {"raw": data.hex(), "note": "Нет конфигурации"}
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

        # ----- Стандартный формат -----
        op = data[off]      # не используется, но пропускаем
        off += 1
        dlen = data[off]
        off += 1
        sdata = data[off:off+dlen]
        off += dlen
        scrc = int.from_bytes(data[off:off+2], "big")   # CRC сенсора (big-endian)
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

    # ---------- Общая CRC ----------
    total_block = data[block_start:off]          # блок от pkt_len до последнего байта перед CRC
    total_crc = int.from_bytes(data[off:off+2], "little")   # CRC пакета в big-endian
    computed_crc = calc_crc16(total_block)
    result["crc_total"] = total_crc
    result["crc_total_valid"] = (computed_crc == total_crc)

    return json.dumps(result, indent=2, ensure_ascii=False)


if __name__ == "__main__":
    hex_input = (
        "89 70 10 18 29 25 56 46 91 87 03 0D 42 2F 4D 11 3B 03 0D 42 2F 1A 05 15 10 27 0E 01 24 BC DF 02 71 3F 00 00 00 00 00 06 00 20 94 A7 D2 01 87 CD 55 00 29 9E 15 04 07 03 0E 00 00 00 DF 00 00 00 4E 00 00 00 00 00 00 3A 67 C4 D9"
    )
    json_str = decode_hex_packet(hex_input, server_time="2026-05-14 12:00:00")
    print(json_str)