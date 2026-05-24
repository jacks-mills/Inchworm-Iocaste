#!/usr/bin/env bash

sudo systemctl stop mosquitto
mosquitto -c ./mqtt.conf -v
