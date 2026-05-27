from flask import Flask, render_template, Response

import json

import threading

import queue

import time

import zmq



app = Flask(__name__)



subscribers_lock = threading.Lock()

subscribers = []  # subscribers receiving data from index_stream



ZMQ_LISTEN_PORT = "5555"





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





def broadcast(msg):

    with subscribers_lock:

        for q in subscribers:

            q.put(msg)





def thread_zmq_server():

    context = zmq.Context()

    socket = context.socket(zmq.PULL)



    socket.bind(f"tcp://0.0.0.0:{ZMQ_LISTEN_PORT}")

    print(f"ZeroMQ server listening on port {ZMQ_LISTEN_PORT}...")



    while True:

        try:

            message_string = socket.recv_string()

            payload = json.loads(message_string)



            mtype = payload.get("message_type")

            b64_image = payload.get("image_data")

            width = payload.get("width")

            height = payload.get("height")

            distance = payload.get("distance")

            angle = payload.get("angle")



            if mtype == "image":

                if b64_image and not b64_image.startswith("data:image"):

                    b64_image = f"image/jpeg;base64,{b64_image}"

                elif b64_image and b64_image.startswith("data:image/jpeg;base64,"):

                    b64_image = b64_image.replace("data:image/jpeg;base64,", "image/jpeg,")

               

                broadcast(f"image:{b64_image}")



                if distance is not None:

                    broadcast(f"distance:{distance}")

                   

                # if angle is not None:

                #     broadcast(f"angle:{angle}")

           

            socket.send_string(json.dumps({"status": "SUCCESS"}))

        except json.JSONDecodeError:

            print("Received malformed JSON string payload over ZeroMQ")

            socket.send_string(json.dumps({"status": "JSON_ERROR"}))

        except Exception as e:

            print(f"Error handling ZeroMQ pipeline payload: {e}")

            time.sleep(0.1)





if __name__ == "__main__":

    # Start the ZeroMQ streaming server loop

    threading.Thread(target=thread_zmq_server, daemon=True).start()

   

    # Run the web application

    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True) 