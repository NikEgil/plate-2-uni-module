import json


def calc_crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return crc & 0xFFFF


def load_config(path="sensor_config.json") -> dict:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return {"field_mapping": {}}


def parse_wh65lp(raw: bytes) -> dict:
    if len(raw) < 21:
        return {"error": "Too short"}
    d = raw
    # Wind Dir (9 bit)
    dir_val = d[2] + ((d[3] & 0x80) << 1)
    # Temp (11 bit)
    tmp_val = d[4] + ((d[3] & 0x07) << 8)
    # Humidity
    hum = d[5]
    # Wind Speed (9/10 bit)
    wsp = d[6] + ((d[3] & 0x10) << 4) if (d[3] & 0x04) else d[6]
    # Gust
    gust = d[7]
    # Rain (16 bit)
    rain = (d[8] << 8) | d[9]
    # UV (16 bit)
    uv = (d[10] << 8) | d[11]
    # Light (24 bit)
    light = (d[12] << 16) | (d[13] << 8) | d[14]
    # Pressure (24 bit)
    pres = (d[17] << 16) | (d[18] << 8) | d[19] if len(d) > 19 else 0

    return {
        "wind_direction": dir_val if dir_val != 0x1FF else 0,
        "temperature": (tmp_val - 400) / 10.0 if tmp_val != 0x7FF else 0,
        "humidity": hum if hum != 0xFF else 0,
        "wind_speed": (wsp / 8.0 * 0.51) if wsp != 0x1FF else 0,
        "wind_gust": gust * 0.51 if gust != 0xFF else 0,
        "rainfall": rain * 0.254,
        "uv": uv if uv != 0xFFFF else 0,
        "light": light / 10.0 if light != 0xFFFFFF else 0,
        "pressure": pres / 100.0 if pres != 0xFFFFFF else 0,
    }


def wh65lp_to_meteo(weather: dict) -> bytes:
    arr = [
        weather["temperature"] * 10,
        weather["humidity"],
        weather["pressure"] * 10,
        weather["wind_direction"],
        weather["wind_speed"],
        weather["wind_gust"],
        weather["uv"],
        weather["light"] / 5,
        weather["rainfall"] * 100,
    ]
    out = bytearray(18)
    for i in range(9):
        v = int(arr[i]) & 0xFFFF
        out[i * 2], out[i * 2 + 1] = (v >> 8) & 0xFF, v & 0xFF
    return bytes(out)


def decode_sensor(data: bytes, sid: int, cfg: dict) -> dict:
    res = {"id_sens": sid, "raw": data.hex().upper(), "decoded": {}}
    fmap = cfg.get("field_mapping", {}).get(str(sid), [])
    if not fmap:
        res["decoded"] = {"raw": data.hex(), "note": "Нет конфигурации"}
        return res
    for f in fmap:
        off = (f["byte"] - 1) * 2
        if off + 2 <= len(data):
            raw = int.from_bytes(data[off : off + 2], "big")
            res["decoded"][f["name"]] = {
                "raw": raw,
                "coef": f["coef"],
                "value": raw * f["coef"],
            }
    return res


def parse_packet(hex_str: str, config: dict = None) -> dict:
    if config is None:
        config = load_config()
    data = bytes.fromhex(hex_str.strip())
    res, off = {}, 0

    # Header
    res["header"] = {
        "ccid": data[0:10].hex().upper(),
        "module_id": int.from_bytes(data[10:13], "big"),
        "bat_tx": data[13] / 10,
        "signal": data[14],
        "rssi": data[15],
    }
    off = 16

    # Footer
    len_off = off
    pkt_len = data[off]
    off += 1
    res["footer"] = {
        "pkt_len": pkt_len,
        "mod_id": int.from_bytes(data[off : off + 3], "big"),
        "bat_mem": data[off + 3] / 10,
        "date": f"20{data[off+4]:02d}-{data[off+5]:02d}-{data[off+6]:02d} {data[off+7]:02d}:{data[off+8]:02d}:{data[off+9]:02d}",
    }
    off += 10

    # Sensors
    sensors = []
    while off < len(data) - 2:
        port = data[off]
        off += 1
        if port == 0:
            break

        sid, op, dlen = data[off], data[off + 1], data[off + 2]
        off += 3
        sdata = data[off : off + dlen]
        off += dlen
        scrc = int.from_bytes(data[off : off + 2], "little")
        off += 2

        # CRC сенсора (с ID до конца данных)
        crc_block = data[off - dlen - 3 : off - 2]
        valid_scrc = calc_crc16(crc_block) == scrc

        # Спец. обработка WH65LP
        if sid == 36 and dlen == 21:
            weather = parse_wh65lp(sdata)
            meteo = wh65lp_to_meteo(weather)
            parsed = decode_sensor(meteo, sid, config)
            parsed["wh65lp_raw"] = weather
        else:
            parsed = decode_sensor(sdata, sid, config)

        sensors.append(
            {
                "port": port,
                "id": sid,
                "op": op,
                "len": dlen,
                "data": sdata.hex().upper(),
                "crc": scrc,
                "crc_valid": valid_scrc,
                "parsed": parsed,
            }
        )
    res["sensors"] = sensors

    # Total CRC
    total_block = data[len_off:off]
    tcrc = int.from_bytes(data[off : off + 2], "little")
    res["crc_total"] = tcrc
    res["crc_total_valid"] = calc_crc16(total_block) == tcrc
    return res


# === ТЕСТ ===
if __name__ == "__main__":
    hex_input = "89701018292556469120000015224D0F56000015281A0501041A2701080304000000FAE3700407030E000001040000001E0000000000001A4603240315246665E24D370D0300160000005F42314D018F6AFA8A91B2C4"
    result = parse_packet(hex_input)
    print(json.dumps(result, indent=2, ensure_ascii=False))
