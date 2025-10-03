import asyncio
import traceback
from bleak import BleakClient, BleakScanner

UUID_STATS_CHAR = "00004004-1212-efde-1523-785feabcd123".lower()
CONNECT_TIMEOUT = 10.0


async def send_uint8(address: str):
    async with BleakClient(address, timeout=CONNECT_TIMEOUT) as client:
        if not client.is_connected:
            raise RuntimeError("Failed to connect")

        # Prepare a 1-byte payload with the value 1
        payload = bytes([1])  # uint8 value 1

        # Write the payload
        await client.write_gatt_char(UUID_STATS_CHAR, payload, response=True)
        print("Sent value 1 (uint8) to characteristic", UUID_STATS_CHAR)


async def main():
    print("Scanning for 5s…")
    devices = await BleakScanner.discover(timeout=5.0)
    if not devices:
        print("No BLE devices found.")
        return

    # Show devices
    for i, d in enumerate(devices):
        print(f"{i:2d}: {d.name or '?':20s} {d.address}")
    idx = int(input("Select device index: "))
    dev = devices[idx]

    try:
        await send_uint8(dev.address)
    except Exception as e:
        print("\n--- ERROR ---")
        print(f"Type: {type(e).__name__}")
        print(f"Message: {e}")
        print("Traceback:")
        traceback.print_exc()


if __name__ == "__main__":
    asyncio.run(main())
