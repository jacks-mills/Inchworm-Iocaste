from flask import Flask, render_template, request, Response, redirect, url_for
import random
import subprocess
import threading
import queue
import json
import time
import glob, os
import paho.mqtt.client as paho
import paho.mqtt.publish as publish



app = Flask(__name__)

subscribers_lock = threading.Lock()
subscribers = []  # subscribers receiveing data from index_stream

MQTT_BROKER_HOSTNAME = "172.20.10.10"


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
    message = {"message type": "brake request", "data": 100}
    publish.single(
            topic="brake/request",
            payload=json.dumps(message),
            hostname=MQTT_BROKER_HOSTNAME)
    return "", 204

def broadcast(msg):
    with subscribers_lock:
        for q in subscribers:
            q.put(msg)

def handle_message(data):
    mtype = data.get("message_type")

    if mtype == "brake state":
        percentage = data.get("data")
        if percentage:
            broadcast(f"brake-percentage:{percentage}")

    elif mtype == "image and distance":
        inner = data.get("data") or {}
        image = inner.get("image")
        distance = inner.get("distance")

        if image:
            broadcast(f"image:image/jpeg,{image}")

        if distance:
            broadcast(f"distance:{distance}")


def thread_mqtt_sub():
    def on_message(mosq, obj, msg):
        print(f"{msg.topic:<20} {msg.qos} {msg.payload.decode()}")
        try:
            handle_message(json.loads(msg.payload))
        except json.JSONDecodeError:
            pass

    def on_publish(mosq, obj, mid):
        pass

    client = paho.Client()
    client.on_message = on_message
    client.on_publish = on_publish

    client.connect("172.20.10.10", 1883, 60)

    client.subscribe("brake/state", 0)
    client.subscribe("brake/jetson", 0) #feel free to change

    while client.loop() == 0:
        pass

def thread_random_brake():
    while True:
        time.sleep(1)
        brake = random.randint(0, 100)
        print(f"brake %:{brake}")
        broadcast(f"brake-percentage:{brake}")

def thread_random_image():
    b64_dir = "b64-montes"
    files = glob.glob(os.path.join(b64_dir, "*.b64"))
    while True:
        for filepath in files:
            with open(filepath, "r") as f:
                b64data = f.read().strip()
            print(f"image:{filepath}")
            broadcast(f"image:image/jpeg,{b64data}")
            time.sleep(3)

if __name__ == "__main__":
    #threading.Thread(target=thread_random_brake, daemon=True).start()
    #threading.Thread(target=thread_random_image, daemon=True).start()
    threading.Thread(target=thread_mqtt_sub, daemon=True).start()
    app.run(debug=False, threaded=True)
