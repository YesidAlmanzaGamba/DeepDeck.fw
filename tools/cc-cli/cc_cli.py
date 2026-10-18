"""Command line client for the DeepDeck configuration protocol (docs/protocol.md).

    python cc_cli.py --usb COM3 call device.info
    python cc_cli.py --ble call layers.get '{"uuid":"..."}'
    python cc_cli.py --ble watch                  # print events, e.g. layer.changed
    python cc_cli.py --usb COM3 selftest          # every read method, plus reversible writes
    python cc_cli.py --usb COM3 --ble selftest    # same, and compare both transports

--ble opens the DeepDeck already paired with this Windows PC (no scan; it does not advertise while
connected). Install: python -m pip install bleak pyserial
"""

import argparse
import asyncio
import json
import sys
import threading
import time

SERVICE_UUID = "dd0e0001-7f4b-4c1a-9d3e-2a6b1c5e8f00"
RX_UUID = "dd0e0002-7f4b-4c1a-9d3e-2a6b1c5e8f00"
TX_UUID = "dd0e0003-7f4b-4c1a-9d3e-2a6b1c5e8f00"
USB_PREFIX = "@DD "
DEFAULT_NAME = "Ahuyama"


class DeviceError(Exception):
    def __init__(self, code, message):
        super().__init__(f"{code}: {message}")
        self.code = code


class Client:
    """Request/response over one transport; events go to self.events."""

    def __init__(self, name):
        self.name = name
        self.next_id = 1
        self.pending = {}
        self.events = asyncio.Queue()
        self.loop = None

    def _on_line(self, line):
        try:
            msg = json.loads(line)
        except ValueError:
            print(f"[{self.name}] unparsable line: {line[:120]}", file=sys.stderr)
            return
        if "event" in msg:
            self.events.put_nowait(msg)
        elif msg.get("id") in self.pending:
            self.pending.pop(msg["id"]).set_result(msg)

    async def request(self, method, params=None, timeout=10):
        msg_id = self.next_id
        self.next_id += 1
        msg = {"id": msg_id, "method": method}
        if params is not None:
            msg["params"] = params
        future = self.loop.create_future()
        self.pending[msg_id] = future
        await self._send(json.dumps(msg, separators=(",", ":"), ensure_ascii=False))
        response = await asyncio.wait_for(future, timeout)
        if "error" in response:
            raise DeviceError(response["error"].get("code"), response["error"].get("message"))
        return response.get("result")


class UsbClient(Client):
    def __init__(self, port):
        super().__init__("usb")
        self.port = port

    async def open(self):
        import serial

        self.loop = asyncio.get_running_loop()
        self.serial = serial.Serial()
        self.serial.port = self.port
        self.serial.baudrate = 115200
        self.serial.timeout = 0.2
        self.serial.dtr = False  # DTR/RTS drive the auto-reset circuit
        self.serial.rts = False
        self.serial.open()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while self.serial.is_open:
            try:
                raw = self.serial.readline()
            except Exception:
                break
            line = raw.decode(errors="replace").rstrip("\r\n")
            if line.startswith(USB_PREFIX):
                self.loop.call_soon_threadsafe(self._on_line, line[len(USB_PREFIX):])

    async def _send(self, line):
        self.serial.write((USB_PREFIX + line + "\n").encode())

    async def close(self):
        self.serial.close()


class BleClient(Client):
    def __init__(self, device_name):
        super().__init__("ble")
        self.device_name = device_name
        self.buffer = bytearray()

    async def open(self):
        from bleak import BleakClient
        from bleak.backends.device import BLEDevice
        from winrt.windows.devices.bluetooth import BluetoothLEDevice
        from winrt.windows.devices.enumeration import DeviceInformation

        self.loop = asyncio.get_running_loop()
        selector = BluetoothLEDevice.get_device_selector_from_pairing_state(True)
        infos = await DeviceInformation.find_all_async_aqs_filter(selector)
        match = [i for i in infos if self.device_name.lower() in (i.name or "").lower()]
        if not match:
            raise SystemExit(f"no paired BLE device named {self.device_name!r}: pair the DeepDeck first")
        dev = await BluetoothLEDevice.from_id_async(match[0].id)
        raw = dev.bluetooth_address
        address = ":".join(f"{(raw >> (8 * i)) & 0xFF:02X}" for i in reversed(range(6)))
        self.client = BleakClient(BLEDevice(address, dev.name, None), timeout=20)
        await self.client.connect()
        if self.client.services.get_service(SERVICE_UUID) is None:
            raise SystemExit("config service not found: remove and pair the DeepDeck again in Windows")
        await self.client.start_notify(TX_UUID, self._on_notify)

    def _on_notify(self, _, data):
        self.buffer.extend(data)
        while b"\n" in self.buffer:
            line, _, rest = self.buffer.partition(b"\n")
            self.buffer[:] = rest
            self._on_line(line.decode(errors="replace"))

    async def _send(self, line):
        data = (line + "\n").encode()
        chunk = max(20, self.client.mtu_size - 3)
        for i in range(0, len(data), chunk):
            await self.client.write_gatt_char(RX_UUID, data[i:i + chunk], response=False)

    async def close(self):
        await self.client.disconnect()


# ------------------------------------------------------------------------------------------------
# selftest

class Report:
    def __init__(self):
        self.failures = 0

    def ok(self, what):
        print(f"  ok    {what}")

    def fail(self, what):
        self.failures += 1
        print(f"  FAIL  {what}")

    def check(self, condition, what):
        (self.ok if condition else self.fail)(what)


async def selftest(clients):
    report = Report()
    main = clients[0]
    print(f"== read round over {', '.join(c.name for c in clients)}")
    snapshots = {}
    for client in clients:
        snap = {}
        for method in ("device.info", "layers.list", "macros.list", "led.get", "wifi.status"):
            try:
                snap[method] = await client.request(method)
                report.ok(f"{client.name} {method}")
            except Exception as err:
                report.fail(f"{client.name} {method}: {err}")
        layers = (snap.get("layers.list") or {}).get("data", [])
        for layer in layers:
            try:
                got = await client.request("layers.get", {"uuid": layer["uuid"]})
                report.check(got.get("name") == layer["name"], f"{client.name} layers.get {layer['name']}")
                snap[f"layer:{layer['uuid']}"] = got
            except Exception as err:
                report.fail(f"{client.name} layers.get {layer.get('name')}: {err}")
        try:
            await client.request("no.such.method")
            report.fail(f"{client.name} unknown method accepted")
        except DeviceError as err:
            report.check(err.code == "unknown_method", f"{client.name} unknown method rejected")
        snapshots[client.name] = snap

    if len(clients) > 1:
        a, b = (snapshots[c.name] for c in clients[:2])
        for key in sorted(set(a) & set(b)):
            if key in ("device.info", "wifi.status"):
                continue  # uptime, heap and rssi change between calls
            report.check(a[key] == b[key], f"{clients[0].name} == {clients[1].name}: {key}")

    print(f"== write round over {main.name} (restored at the end)")
    snap = snapshots[main.name]
    layers = (snap.get("layers.list") or {}).get("data", [])
    if layers:
        uuid = layers[0]["uuid"]
        original = snap[f"layer:{uuid}"]
        renamed = dict(original, uuid=uuid, name="cc_test")
        try:
            await main.request("layers.update", renamed)
            got = await main.request("layers.get", {"uuid": uuid})
            report.check(got["name"] == "cc_test", "layers.update renames the layer")
        except Exception as err:
            report.fail(f"layers.update: {err}")
        finally:
            await main.request("layers.update", dict(original, uuid=uuid))
            got = await main.request("layers.get", {"uuid": uuid})
            report.check(got == original, "layer restored to its original content")

        try:
            await main.request("layers.update", {"uuid": uuid, "name": "x"})
            report.fail("layers.update without rows accepted")
        except DeviceError as err:
            report.check(err.code == "bad_request", "layers.update without rows rejected")

        try:
            while not main.events.empty():  # events from the writes above
                main.events.get_nowait()
            await main.request("layers.set_active", {"uuid": uuid})
            deadline = time.time() + 3
            seen = False
            while not seen and time.time() < deadline:
                event = await asyncio.wait_for(main.events.get(), max(0.1, deadline - time.time()))
                seen = event["event"] == "layer.changed" and event["data"].get("uuid") == uuid
            report.check(seen, "layers.set_active sends layer.changed")
        except Exception as err:
            report.fail(f"layers.set_active / layer.changed: {err}")
        finally:
            active = ((snap.get("device.info") or {}).get("layer") or {}).get("uuid")
            if active:
                await main.request("layers.set_active", {"uuid": active})

    macros = (snap.get("macros.list") or {}).get("macros", [])
    if macros:
        original = macros[-1]
        try:
            await main.request("macros.update", dict(original, name="cctest", key=[224, 4]))
            got = (await main.request("macros.list"))["macros"][-1]
            report.check(got["name"] == "cctest" and got["key"][:2] == [224, 4], "macros.update changes the macro")
            await main.request("macros.delete", {"keycode": original["keycode"]})
            got = (await main.request("macros.list"))["macros"][-1]
            report.check(got["name"] == "" and not any(got["key"]), "macros.delete empties the macro")
        except Exception as err:
            report.fail(f"macros update/delete: {err}")
        finally:
            await main.request("macros.update", original)
            got = (await main.request("macros.list"))["macros"][-1]
            report.check(got == original, "macro restored")
        try:
            await main.request("macros.update", dict(original, key=[999]))
            report.fail("macro key code 999 accepted")
        except DeviceError as err:
            report.check(err.code == "bad_request", "macro key code 999 rejected")

    led = snap.get("led.get")
    if led:
        try:
            await main.request("led.set", dict(led, V=max(0, led.get("V", 0) - 1)))
            report.check((await main.request("led.get"))["V"] == max(0, led.get("V", 0) - 1), "led.set changes V")
        except Exception as err:
            report.fail(f"led.set: {err}")
        finally:
            await main.request("led.set", led)
            report.check(await main.request("led.get") == led, "LED mode restored")

    print(f"== {report.failures} failure(s)")
    return report.failures


# ------------------------------------------------------------------------------------------------

async def run(args):
    clients = []
    if args.usb:
        clients.append(UsbClient(args.usb))
    if args.ble:
        clients.append(BleClient(args.name))
    if not clients:
        raise SystemExit("choose --usb PORT and/or --ble")
    for client in clients:
        await client.open()
    try:
        if args.command == "call":
            params = json.loads(args.params) if args.params else None
            for client in clients:
                started = time.time()
                result = await client.request(args.method, params)
                print(f"[{client.name}] {int((time.time() - started) * 1000)} ms")
                print(json.dumps(result, indent=2, ensure_ascii=False))
        elif args.command == "watch":
            print("watching events, Ctrl+C to stop")
            async def pump(client):
                while True:
                    event = await client.events.get()
                    print(f"[{client.name}] {json.dumps(event, ensure_ascii=False)}")
            await asyncio.gather(*(pump(c) for c in clients))
        elif args.command == "selftest":
            return await selftest(clients)
    finally:
        for client in clients:
            await client.close()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--usb", metavar="PORT", help="serial port, e.g. COM3")
    parser.add_argument("--ble", action="store_true", help="paired DeepDeck over BLE (Windows)")
    parser.add_argument("--name", default=DEFAULT_NAME, help="BLE name of the paired DeepDeck")
    sub = parser.add_subparsers(dest="command", required=True)
    call = sub.add_parser("call")
    call.add_argument("method")
    call.add_argument("params", nargs="?", help="params as JSON")
    sub.add_parser("watch")
    sub.add_parser("selftest")
    args = parser.parse_args()
    try:
        sys.exit(asyncio.run(run(args)) or 0)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
