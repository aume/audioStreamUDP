import argparse
import serial
import time
from pythonosc import dispatcher
from pythonosc import osc_server
from DMXEnttecPro import Controller

chanmap = {'dimmer':1,'red':3,'green':4, 'blue':5, 'amber':6, 'lime':7, 'wheel':9, 'temp':10, 'gobo':11}

# Send DMX data to the Enttec DMX Pro
def send_dmx(dmx, values):
    #dimmer, red, green, blue, amber, lime, wheel, temp, gobo
    chans = [chanmap[ch] for ch in ]
    for i in range(len(values)):
        dmx.set_channel(chans[i], values[i])
    dmx.submit()  # Sends the update to the controller

# Handlers for OSC messages
def osc_handler(unused_addr, args, dimmer, red, green, blue, amber, lime, wheel, temp, gobo):
    #print(f"Received OSC message - Channel: {ch}, Value: {val}")
    send_dmx(args[0], [dimmer, red, green, blue, amber, lime, wheel, temp, gobo])

def rgb(unused_addr, args, red, green, blue):
    dmx = args[0]
    dmx.set_channel(chanmap['red'], red)
    dmx.set_channel(chanmap['green'], green)
    dmx.set_channel(chanmap['blue'], blue)

def red(unused_addr, args, val):
     dmx = args[0]
     dmx.set_channel(chanmap["red"], val)

def green(unused_addr, args, val):
     dmx = args[0]
     dmx.set_channel(chanmap["green"], val)

def blue(unused_addr, args, val):
     dmx = args[0]
     dmx.set_channel(chanmap["blue"], val)

def gobo(unused_addr, args, val):
     dmx = args[0]
     dmx.set_channel(chanmap["gobo"], val)

def wheel(unused_addr, args, val):
     dmx = args[0]
     dmx.set_channel(chanmap["wheel"], val)

def amber(unused_addr, args, val):
     dmx = args[0]
     dmx.set_channel(chanmap["amber"], val)

def lime(unused_addr, args, val):
     dmx = args[0]
     dmx.set_channel(chanmap["lime"], val)

def temp(unused_addr, args, val):
     dmx = args[0]
     dmx.set_channel(chanmap["temp"], val)

def dimmer(unused_addr, args, val):
     dmx = args[0]
     dmx.set_channel(chanmap["dimmer"], val)

if __name__ == "__main__":
    # Setup the DMX Pro device
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default='192.168.6.225', help="The ip to listen on")
    parser.add_argument("--port", type=int, default=8000, help="The port to listen on")
    parser.add_argument("--device", default="/dev/ttyUSB0", help="The DMX device")
    args = parser.parse_args()

    #ser = setup_dmx(args.device)
    dmx = Controller(args.device, auto_submit=True)

    dispatcher = dispatcher.Dispatcher()
    dispatcher.map("/dmx", osc_handler, dmx)
    dispatcher.map("/rgb", rgb, dmx)
    dispatcher.map("/gobo", gobo, dmx)
    dispatcher.map("/wheel", wheel, dmx)
    dispatcher.map("/dimmer", dimmer, dmx)
    dispatcher.map("/amber", amber, dmx)
    dispatcher.map("/lime", lime, dmx)
    dispatcher.map("/temp", temp, dmx)
    dispatcher.map("/red", red, dmx)
    dispatcher.map("/green", green, dmx)
    dispatcher.map("/blue", blue, dmx)

    server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), dispatcher)
    print(f"Serving on {server.server_address}")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Exiting")
        dmx.close()
