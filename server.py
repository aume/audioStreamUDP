import socket   
import sys
import pyaudio
import time

HOST = '10.0.0.189' # send to this IP
PORT = 50007 # with this port

CHUNK = 1024
FORMAT = pyaudio.paInt16
CHANNELS = 1
RATE = 44100
BUFF_SIZE = 65536

# pyaudio call back function to send packets
def callback(in_data, frame_count, time_info, status):
    server_socket.sendto(in_data, (HOST, PORT))    
    return (in_data, pyaudio.paContinue)

# Instantiate PyAudio
p_audio = pyaudio.PyAudio()

# create dgram udp socket
try:
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    #server_socket.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,BUFF_SIZE)
except socket.error:
    print('Failed to create socket')
    sys.exit()

# Open stream using callback
stream = p_audio.open(format=FORMAT,
            channels=CHANNELS,
            rate=RATE,
            frames_per_buffer=CHUNK,
            input=True,
            output=False,
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