import argparse
import serial
import time
from pythonosc import dispatcher
from pythonosc import osc_server
from DMXEnttecPro import Controller

# Setup the DMX Pro device


# Send DMX data to the Enttec DMX Pro
def send_dmx(dmx, channel, value):
    dmx.set_channel(channel, value)  # Sets DMX channel 1 to max 255
    dmx.submit()  # Sends the update to the controller

# Handler for OSC messages
def osc_handler(unused_addr, args, ch, val):
    print(f"Received OSC message - Channel: {ch}, Value: {val}")
    send_dmx(args[0], ch, val)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="192.168.2.249", help="The ip to listen on")
    parser.add_argument("--port", type=int, default=8000, help="The port to listen on")
    parser.add_argument("--device", default="/dev/ttyUSB0", help="The DMX device")
    args = parser.parse_args()

    #ser = setup_dmx(args.device)
    dmx = Controller(args.device, auto_submit=True)

    dispatcher = dispatcher.Dispatcher()
    dispatcher.map("/dmx", osc_handler, dmx)

    server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), dispatcher)
    print(f"Serving on {server.server_address}")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Exiting")
        dmx.close()
