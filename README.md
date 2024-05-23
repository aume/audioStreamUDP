# audioStreamUDP
A straght forward point to point audio over UDP streaming server and client, transmitting 16 bit 44100 Hz PCM over W/LAN.


# AudioStreamServer.py
## Requires
pyaudio  
numpy  
tkinter  
pyinstaller (if packaging into executable)

## Usage
Change to suit needs:  
self.sample_rate = 44100   
self.chunk_size = 1024   
self.interface = 'BlackHole 16ch'  
  
Launch script:
python AudioStreamServer.py  
Interface is self explanitory for adding and modifying clients.  
The list of clients can be saved and loaded from a json file.  

# AudioStreamClient.py
Change to match server parameters
client_port = 57001  
rate = 44100     
chunk_size = 1024  
    
Launch script:
python AudioStreamClient.py  

# To create executable
pyinstaller audio_stream_server.spec

  
# (Deprecated)
# server.py

## Requires: 
portaudio
pyaudio

Tested with server running from M2 mac laptop and client on RPi4 over WLAN
Minimal frame drops was a happy expereince. 

# Server Settings
CHUNK = 2048
RATE = 44100
BUFF_SIZE = 65536
FORMAT = pyaudio.paInt16

AUDIO_INTERFACE = 'MacBook Air Microphone'#'BlackHole 16ch'

PORT = 50007 # send to this port
## multicast client list
CLIENTS = ['127.0.0.1', 
           '192.168.1.3', 
           '10.0.0.1', 
           '10.0.0.4', 
           '10.0.0.3', 
           '10.0.0.2'] # a list of clients to send packets

## specify which audio channel to send to which client
CHANNEL_MAP = [{'ch':0,'ip':CLIENTS[0]},
               {'ch':0,'ip':CLIENTS[1]},
               ]


# client.py Client Settings
HOST = '' # Symbolic name for all available (no need to change) 

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
and
https://gist.github.com/ViennaMike/70c6a47ad5309a06f03faed047b1df11

multichannel capability adapted from
https://github.com/Tomlevron/multi-channel-audio-recorder/blob/main/audio_recorder.py
