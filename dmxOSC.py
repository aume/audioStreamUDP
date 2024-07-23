import argparse
import serial
import time
from pythonosc import dispatcher
from pythonosc import osc_server

# Setup the DMX Pro device
def setup_dmx(device):
    ser = serial.Serial(device, baudrate=250000)
    ser.write([0])  # Reset DMX
    return ser

# Send DMX data to the Enttec DMX Pro
def send_dmx(ser, channel, value):
    data = [0] * 513  # DMX data for 512 channels + start byte
    data[0] = 0x7E  # Start byte
    data[1] = 6  # Send DMX data label
    data[2] = len(data) & 0xFF  # LSB of data length
    data[3] = (len(data) >> 8) & 0xFF  # MSB of data length
    data[4] = 0  # Start code
    data[channel + 4] = value  # Set the value for the specified channel
    data[-1] = 0xE7  # End byte
    ser.write(bytearray(data))

# Handler for OSC messages
def osc_handler(unused_addr, args, ch, val):
    print(f"Received OSC message - Channel: {ch}, Value: {val}")
    send_dmx(args[0], ch, val)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="127.0.0.1", help="The ip to listen on")
    parser.add_argument("--port", type=int, default=8000, help="The port to listen on")
    parser.add_argument("--device", default="/dev/ttyUSB0", help="The DMX device")
    args = parser.parse_args()

    ser = setup_dmx(args.device)

    dispatcher = dispatcher.Dispatcher()
    dispatcher.map("/dmx", osc_handler, ser)

    server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), dispatcher)
    print(f"Serving on {server.server_address}")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Exiting")
        ser.close()