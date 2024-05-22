import socket
import pyaudio
import threading

class AudioStreamClient:
    def __init__(self, ip, port, channels, rate, chunk_size):
        self.ip = ip
        self.port = port
        self.channels = channels
        self.rate = rate
        self.chunk_size = chunk_size
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((self.ip, self.port))

        self.audio = pyaudio.PyAudio()
        self.stream = self.audio.open(format=pyaudio.paInt16,
                                      channels=self.channels,
                                      rate=self.rate,
                                      output=True,
                                      frames_per_buffer=self.chunk_size)
        self.is_receiving = False

    def start_receiving(self):
        self.is_receiving = True
        self.thread = threading.Thread(target=self.receive_audio)
        self.thread.start()
        print("Started receiving audio")

    def stop_receiving(self):
        self.is_receiving = False
        self.thread.join()
        print("Stopped receiving audio")

    def receive_audio(self):
        while self.is_receiving:
            data, _ = self.sock.recvfrom(self.chunk_size * self.channels * 2)
            self.stream.write(data)

    def close(self):
        self.stop_receiving()
        self.stream.stop_stream()
        self.stream.close()
        self.audio.terminate()
        self.sock.close()

if __name__ == "__main__":
    client_ip = "0.0.0.0"  # Client's IP address
    client_port = 57001      # Client's port to receive audio data
    channels = 1            # Number of audio channels (e.g., mono)
    rate = 44100            # Sample rate in Hz
    chunk_size = 1024       # Number of audio frames per buffer

    client = AudioStreamClient(client_ip, client_port, channels, rate, chunk_size)
    client.start_receiving()

    try:
        while True:
            pass
    except KeyboardInterrupt:
        client.close()
