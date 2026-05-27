from flask import Flask, render_template, request, Response, redirect, url_for
import random
import threading
import queue
import json
import time
import glob, os
import asyncio
from bleak import BleakScanner, BleakClient

app = Flask(__name__)

subscribers_lock = threading.Lock()
subscribers = []  # subscribers receiving data from index_stream

DEVICE_NAME = "BrakeController"

# Nordic UART Service UUIDs
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # write to device
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # notify from device

# Shared BLE state
ble_client: BleakClient | None = None
ble_lock = threading.Lock()
ble_loop: asyncio.AbstractEventLoop | None = None


def broadcast(msg):
    with subscribers_lock:
        for q in subscribers:
            q.put(msg)


def handle_message(data):
    mtype = data.get("message_type")
    if mtype == "brake state":
        percentage = data.get("data")
        if percentage is not None:
            broadcast(f"brake-percentage:{percentage}")
    elif mtype == "image and distance":
        inner = data.get("data") or {}
        image = inner.get("image")
        distance = inner.get("distance")
        if image:
            broadcast(f"image:image/jpeg,{image}")
        if distance:
            broadcast(f"distance:{distance}")


def ble_send(payload: dict):
    """Send a JSON payload to the board over NUS RX. Thread-safe."""
    with ble_lock:
        client = ble_client

    if client is None or not client.is_connected:
        print("BLE: not connected, dropping message")
        return

    data = json.dumps(payload).encode("utf-8")

    async def _write():
        await client.write_gatt_char(NUS_RX_UUID, data, response=False)

    if ble_loop is not None:
        asyncio.run_coroutine_threadsafe(_write(), ble_loop).result(timeout=5)


@app.route("/", methods=["GET"])
def index():
    return render_template("index.html")


@app.route("/stream")
def index_stream():
    q = queue.Queue()
    with subscribers_lock:
        subscribers.append(q)

    def event_stream():
        try:
            while True:
                yield f"data: {q.get()}\n\n"
        finally:
            with subscribers_lock:
                subscribers.remove(q)

    return Response(event_stream(), mimetype="text/event-stream")


@app.route("/brake", methods=["POST"])
def brake():
    print("brake pressed")
    ble_send({"message_type": "brake request", "sender": "pc", "percentage": 100})
    return "", 204


@app.route("/release", methods=["POST"])
def release():
    print("release pressed")
    ble_send({"message_type": "brake request", "sender": "pc", "percentage": 0})
    return "", 204


def on_nus_notification(sender, data: bytearray):
    """Called on every NUS TX notification from the board."""
    try:
        msg = json.loads(data.decode("utf-8"))
        print(f"BLE RX: {msg}")
        handle_message(msg)
    except (json.JSONDecodeError, UnicodeDecodeError):
        print(f"BLE RX (raw): {data.hex()}")


async def ble_run():
    global ble_client

    while True:
        # scan
        print(f"BLE: scanning for '{DEVICE_NAME}'…")
        device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10.0)
        if device is None:
            print("BLE: device not found, retrying in 5 s")
            await asyncio.sleep(5)
            continue

        print(f"BLE: found {device.address}, connecting…")
        try:
            async with BleakClient(device) as client:
                with ble_lock:
                    ble_client = client

                await client.start_notify(NUS_TX_UUID, on_nus_notification)
                print("BLE: connected and subscribed")

                # Stay connected until the link drops
                while client.is_connected:
                    await asyncio.sleep(1)

                await client.stop_notify(NUS_TX_UUID)

        except Exception as e:
            print(f"BLE: error — {e}")
        finally:
            with ble_lock:
                ble_client = None

        print("BLE: disconnected, reconnecting in 3 s…")
        await asyncio.sleep(3)


def thread_ble():
    global ble_loop
    ble_loop = asyncio.new_event_loop()
    asyncio.set_event_loop(ble_loop)
    ble_loop.run_until_complete(ble_run())


if __name__ == "__main__":
    threading.Thread(target=thread_ble, daemon=True).start()
    app.run(debug=False, threaded=True)
