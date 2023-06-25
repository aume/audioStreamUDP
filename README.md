# audioStreamUDP

A straght forward cmd-line point to point audio over UDP streaming server and client, transmitting 16 bit 44100 Hz PCM over W/LAN

# Requires: 
portaudio
pyaudio

Tested with server running from M2 mac laptop and client on RPi4 over WLAN
Minimal frame drops was a happy expereince. 

# Server Settings
HOST = '10.0.0.189' # send to this IP

PORT = 50007 # with this port

CHUNK = 1024

FORMAT = pyaudio.paInt16

CHANNELS = 1

RATE = 44100

BUFF_SIZE = 65536

# Client Settings
HOST = '127.0.0.1' # local

PORT = 50007 # listen on the port

CHUNK = 1024

JITTER_LAG = 0.25

FORMAT = pyaudio.paInt16

CHANNELS = 1

RATE = 44100

BUFF_SIZE = 65536

# To Run
## on the server
python server.py

## on the client
python client

# Authors

This repository created by https://digitalmedia.ok.ubc.ca/spiral/

Client built upon the very useful post 
https://pyshine.com/How-to-send-audio-from-PyAudio-over-socket/

Server apadpted from:
https://github.com/amurzeau/waveOverUDP
