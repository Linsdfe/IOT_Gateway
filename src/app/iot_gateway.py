#!/usr/bin/env python3
import smbus2
import time
import json
import signal
import sys
import os

class SHT30Reader:
    def __init__(self, bus=1, addr=0x44):
        self.bus = smbus2.SMBus(bus)
        self.addr = addr

    def read(self):
        self.bus.write_i2c_block_data(self.addr, 0x2C, [0x06])
        time.sleep(0.02)
        data = self.bus.read_i2c_block_data(self.addr, 0x00, 6)
        raw_temp = (data[0] << 8) | data[1]
        raw_humi = (data[3] << 8) | data[4]
        temp = -45.0 + 175.0 * raw_temp / 65535.0
        humi = 100.0 * raw_humi / 65535.0
        return temp, humi

class BH1750Reader:
    def __init__(self, bus=1, addr=0x23):
        self.bus = smbus2.SMBus(bus)
        self.addr = addr

    def read(self):
        self.bus.write_byte(self.addr, 0x10)
        time.sleep(0.18)
        data = self.bus.read_i2c_block_data(self.addr, 0x00, 2)
        raw = (data[0] << 8) | data[1]
        lux = raw / 1.2
        return lux

class IoTSensorGateway:
    def __init__(self, i2c_bus=1, interval=2):
        self.sht30 = SHT30Reader(i2c_bus, 0x44)
        self.bh1750 = BH1750Reader(i2c_bus, 0x23)
        self.interval = interval
        self.running = False
        self.callbacks = []
        self.data = {"temperature": 0, "humidity": 0, "light": 0, "valid": False}

    def register_callback(self, cb):
        self.callbacks.append(cb)

    def read_sensors(self):
        try:
            temp, humi = self.sht30.read()
            light = self.bh1750.read()
            self.data = {
                "temperature": round(temp, 1),
                "humidity": round(humi, 1),
                "light": round(light, 1),
                "valid": True
            }
            return True
        except Exception as e:
            print(f"[ERROR] sensor read failed: {e}")
            return False

    def start(self):
        self.running = True
        print("[Gateway] started, interval={}s".format(self.interval))
        while self.running:
            if self.read_sensors():
                d = self.data
                print("[Gateway] T={:.1f}C  H={:.1f}%  L={:.1f}lux".format(
                    d["temperature"], d["humidity"], d["light"]))
                for cb in self.callbacks:
                    try:
                        cb(d)
                    except Exception as e:
                        print(f"[ERROR] callback failed: {e}")
            time.sleep(self.interval)

    def stop(self):
        self.running = False
        print("[Gateway] stopped")

def signal_handler(sig, frame):
    if gateway:
        gateway.stop()
    sys.exit(0)

if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal_handler)

    gateway = IoTSensorGateway(i2c_bus=1, interval=2)

    gateway.register_callback(lambda d: None)

    gateway.start()
