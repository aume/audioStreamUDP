import socket   
import sys
import pyaudio
import time

import numpy as np

AUDIO_INTERFACE = 'BlackHole 16ch'


CLIENTS = ['127.0.0.1', 
           '10.0.0.189', 
           '10.0.0.1', 
           '10.0.0.4', 
           '10.0.0.3', 
           '10.0.0.2'] # a list of clients to send packets
PORT = 50007 # with this port

CHANNEL_MAP = [{'ch':15,'ip':CLIENTS[0]},
               {'ch':2,'ip':CLIENTS[1]},
               ]

CHUNK = 1024
FORMAT = pyaudio.paInt16
RATE = 44100
#BUFF_SIZE = 65536

n_channels = -1 ; # reassigned with the find device function

# pyaudio call back function to send packets
def callback(in_data, frame_count, time_info, status):
    data_array = np.frombuffer(in_data, dtype='int16')
    for entity in CHANNEL_MAP:
        channel = data_array[entity['ch']::n_channels] # an error check here for ch<n_channels
        data_str = channel.tobytes()  # Use tobytes instead of tostring
        server_socket.sendto(data_str, (entity['ip'], PORT))    
    return (in_data, pyaudio.paContinue)

# Instantiate PyAudio
p_audio = pyaudio.PyAudio()


# find the index of audio device
def find_device_index(device_name):
    found = -1
    for i in range(p_audio.get_device_count()):
        dev = p_audio.get_device_info_by_index(i)
        name = dev['name']#.encode('utf-8')
        print(i, name, dev['maxInputChannels'], dev['maxOutputChannels'])
        if name.find(device_name) >= 0 and dev['maxInputChannels'] > 0:
            found = i
            n_ch = int(dev['maxInputChannels'])
            break
    return found,n_ch


device_index,n_channels = find_device_index(AUDIO_INTERFACE)
if device_index < 0:
    print('No device found')
    sys.exit(1)

# create dgram udp socket
try:
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    #server_socket.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,BUFF_SIZE)
except socket.error:
    print('Failed to create socket')
    sys.exit()

print(n_channels)
# Open stream using callback
stream = p_audio.open(format=FORMAT,
            channels=n_channels,
            rate=RATE,
            frames_per_buffer=CHUNK,
            input=True,
            output=False,
            input_device_index=device_index,
            stream_callback=callback)    
            
stream.start_stream()

while stream.is_active():
    try :
        # data = stream.read(CHUNK)
        # s.sendto(data, (HOST, PORT))
        time.sleep(1)   
    except socket.error as e:
        print('Error Code : ', e.message, e.args)
        sys.exit()
    except KeyboardInterrupt:   
        print("Stream off.")        
        stream.stop_stream()
        stream.close()
        p_audio.terminate()