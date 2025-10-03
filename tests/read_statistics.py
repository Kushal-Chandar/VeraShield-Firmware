import asyncio, traceback
from bleak import BleakClient, BleakScanner

UUID_STATS_CHAR = "00004003-1212-efde-1523-785feabcd123".lower()
PREFERRED_NAME = None
CONNECT_TIMEOUT = 10.0


def decode_stats(payload: bytes):
    if len(payload) != 9:
        raise ValueError(f"Expected 9 bytes, got {len(payload)}")
    meta = (payload[0] << 8) | payload[1]
    count = meta & 0x3FFF
    state = (meta >> 14) & 0x3
    sec, minute, hour = payload[2], payload[3], payload[4]
    mday, wday, mon, year_off = payload[5], payload[6], payload[7], payload[8]
    return {
        "count": count,
        "state": state,
        "timestamp": {
            "year": 2000 + year_off,
            "month": mon + 1,
            "day": mday,
            "hour": hour,
            "minute": minute,
            "second": sec,
            "wday": wday,
        },
        "raw": payload.hex(" "),
    }


async def pick_device():
    print("Scanning for 5s…")
    devices = await BleakScanner.discover(timeout=5.0)
    if not devices:
        print("No BLE devices found.")
        return None
    if PREFERRED_NAME:
        for d in devices:
            if d.name == PREFERRED_NAME:
                print(f"Selected {d.name} [{d.address}]")
                return d
    for i, d in enumerate(devices):
        print(f"{i:2d}: {d.name or '?':20s} {d.address}")
    while True:
        try:
            idx = int(input("Select device index: "))
            if 0 <= idx < len(devices):
                return devices[idx]
        except Exception:
            pass
        print("Invalid choice.")


async def read_stats(address: str):
    # adapter=ADAPTER is harmless on other OSes; remove if you don’t need it
    async with BleakClient(address, timeout=CONNECT_TIMEOUT) as client:
        if not client.is_connected:
            raise RuntimeError(
                "Connected context entered, but client.is_connected is False."
            )

        # ---- Get services in a way that works across Bleak versions ----
        svcs = None
        get_svcs = getattr(client, "get_services", None)
        if callable(get_svcs):
            svcs = await get_svcs()
        else:
            svcs = getattr(client, "services", None)
            if svcs is None:
                raise RuntimeError("No services available (client.services is None).")

        print("\n=== GATT Database ===")
        target_char = None
        for svc in svcs:
            print(f"SERVICE {svc.uuid}")
            for ch in svc.characteristics:
                props = ",".join(ch.properties)
                print(f"  CHAR    {ch.uuid}  [{props}]")
                if ch.uuid.lower() == UUID_STATS_CHAR:
                    target_char = ch

        if not target_char:
            raise RuntimeError(
                f"Characteristic {UUID_STATS_CHAR} not found. Check your firmware UUID."
            )

        if "read" not in target_char.properties:
            raise RuntimeError(
                f"Characteristic {target_char.uuid} is not readable (props={target_char.properties})."
            )

        payload = await client.read_gatt_char(target_char)
        stats = decode_stats(payload)

        print("\n=== Statistics ===")
        print(f"Count : {stats['count']}")
        print(f"State : {stats['state']}")
        ts = stats["timestamp"]
        print(
            f"Time  : {ts['year']:04d}-{ts['month']:02d}-{ts['day']:02d} "
            f"{ts['hour']:02d}:{ts['minute']:02d}:{ts['second']:02d} (wday={ts['wday']})"
        )
        print(f"Raw   : {stats['raw']}")


async def main():
    dev = await pick_device()
    if not dev:
        return
    try:
        await read_stats(dev.address)
    except Exception as e:
        print("\n--- ERROR ---")
        print(f"Type: {type(e).__name__}")
        print(f"Message: {e}")
        print("Traceback:")
        traceback.print_exc()


if __name__ == "__main__":
    # import logging; logging.basicConfig(level=logging.DEBUG)
    asyncio.run(main())
