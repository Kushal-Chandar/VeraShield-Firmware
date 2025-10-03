import asyncio
from bleak import BleakClient, BleakScanner

# BLE UUIDs (must match your firmware)
UUID_STATS_CHAR = "abcdef01-1234-5678-1234-1234567890ab"

# Optional: put your device name here to auto-select it.
# If left None, the script will list devices and ask you.
PREFERRED_NAME = None  # e.g. "MyZephyrBoard"


def decode_stats(payload: bytes):
    """
    Expect 9 bytes:
      [0..1] meta (BE) -> bits 15..14 = state(2 bits), bits 13..0 = count(14 bits)
      [2] sec, [3] min, [4] hour, [5] mday, [6] wday, [7] mon(0..11), [8] year-2000
    """
    if len(payload) != 9:
        raise ValueError(f"Expected 9 bytes, got {len(payload)}")

    meta = (payload[0] << 8) | payload[1]
    count = meta & 0x3FFF
    state = (meta >> 14) & 0x3

    sec, minute, hour = payload[2], payload[3], payload[4]
    mday, wday, mon, year_off = payload[5], payload[6], payload[7], payload[8]
    year = 2000 + year_off
    # mon is 0..11 in firmware; for human display add +1
    mon_display = mon + 1

    return {
        "count": count,
        "state": state,
        "timestamp": {
            "year": year,
            "month": mon_display,
            "day": mday,
            "hour": hour,
            "minute": minute,
            "second": sec,
            "wday": wday,  # 0..6
        },
        "raw": payload.hex(" "),
    }


async def pick_device():
    devices = await BleakScanner.discover(timeout=5.0)
    if not devices:
        print("No BLE devices found. Make sure your board is advertising.")
        return None

    # Try preferred name first
    if PREFERRED_NAME:
        for d in devices:
            if d.name == PREFERRED_NAME:
                print(f"Selected {d.name} [{d.address}]")
                return d

    print("Discovered devices:")
    for idx, d in enumerate(devices):
        print(f"{idx:2d}: {d.name or '?':20s}  {d.address}")

    while True:
        try:
            choice = int(input("Select device index: "))
            if 0 <= choice < len(devices):
                return devices[choice]
        except Exception:
            pass
        print("Invalid choice. Try again.")


async def read_stats(address: str):
    async with BleakClient(address) as client:
        if not client.is_connected:
            raise RuntimeError("Failed to connect.")
        payload = await client.read_gatt_char(UUID_STATS_CHAR)
        stats = decode_stats(payload)

        print("\n=== Statistics ===")
        print(f"Count : {stats['count']}")
        print(f"State : {stats['state']}")
        ts = stats["timestamp"]
        print(
            "Time  : {y:04d}-{m:02d}-{d:02d} {hh:02d}:{mm:02d}:{ss:02d} (wday={wd})".format(
                y=ts["year"], m=ts["month"], d=ts["day"],
                hh=ts["hour"], mm=ts["minute"], ss=ts["second"], wd=ts["wday"]
            )
        )
        print(f"Raw   : {stats['raw']}")


async def main():
    dev = await pick_device()
    if not dev:
        return
    try:
        await read_stats(dev.address)
    except Exception as e:
        print(f"Error: {e}")


if __name__ == "__main__":
    asyncio.run(main())
