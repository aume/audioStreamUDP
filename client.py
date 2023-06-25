# Welcome to PyShine
# This is client code to receive video and audio frames over UDP
# https://pyshine.com/How-to-send-audio-from-PyAudio-over-socket/
import socket
import threading, wave, pyaudio, time, queue


HOST = '127.0.0.1'
PORT = 50007

CHUNK = 1024
FORMAT = pyaudio.paInt16
CHANNELS = 1
RATE = 44100
BUFF_SIZE = 65536

host_name = socket.gethostname()
host_ip = HOST
print(host_ip)
port = PORT


q = queue.Queue(maxsize=2000)

def audio_stream_UDP():
	BUFF_SIZE = 65536
	client_socket = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
	#client_socket.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,BUFF_SIZE)
	p = pyaudio.PyAudio()
	stream = p.open(format=FORMAT,
					channels=1,
					rate=RATE,
					output=True,
					frames_per_buffer=CHUNK)
					
	# create socket
	# Bind socket to local host and port
	try:
		client_socket.bind((HOST, PORT))
	except socket.error as e:
		print('Bind failed. Error Code : ', e.message, e.args)
		sys.exit()
	
	def getAudioData():
		while True:
			frame,_= client_socket.recvfrom(BUFF_SIZE)
			q.put(frame)
			print('Queue size...',q.qsize())
	t1 = threading.Thread(target=getAudioData, args=())
	t1.start()
	time.sleep(0.1)
	print('Now Playing...')
	while True:
		frame = q.get()
		stream.write(frame)

	client_socket.close()
	print('Audio closed')
	os._exit(1)



t1 = threading.Thread(target=audio_stream_UDP, args=())
t1.start()
