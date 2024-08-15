import argparse
import time
from pythonosc import udp_client

def send_osc_command(client, channel, value):
    print(f"Sending OSC message - Channel: {channel}, Value: {value}")
    client.send_message("/dmx", [channel, value])

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="127.0.0.1", help="The IP address of the DMX controller")
    parser.add_argument("--port", type=int, default=8000, help="The port number of the DMX controller")
    args = parser.parse_args()

    client = udp_client.SimpleUDPClient(args.ip, args.port)

    try:
        while True:
            # Example usage: setting channel 1 to a value of 255 (full brightness)
            channel = int(input("Enter DMX channel (1-512): "))
            value = int(input(f"Enter value for channel {channel} (0-255): "))

            send_osc_command(client, channel, value)

            # Optional: sleep for a short time before sending another command
            time.sleep(0.1)

    except KeyboardInterrupt:
        print("Exiting")