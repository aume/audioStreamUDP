import socket
import sys
import pyaudio
import threading
import tkinter as tk
from tkinter import messagebox, simpledialog
import json
import numpy as np

class AudioStreamServer:
    def __init__(self):
        self.clients = {}
        self.sample_rate = 32000  # Sample rate in Hz
        self.chunk_size = 256  # Number of audio frames per buffer

        self.interface = 'artwalk'#'BlackHole 16ch'

        self.audio = pyaudio.PyAudio()
        # get device ID for named audio inteface
        self.device_index,self.audio_channels = self.find_device_index(self.interface)
        
        if self.device_index < 0:
            print('No device found')
            sys.exit(1)

        # open the stream
        self.stream = self.audio.open(format=pyaudio.paInt16,
                                      channels=self.audio_channels,
                                      rate=self.sample_rate,
                                      input=True,
                                      output=False,
                                      input_device_index=self.device_index,
                                      frames_per_buffer=self.chunk_size,
                                      stream_callback=self.audio_callback)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.is_streaming = False

    def find_device_index(self, device_name):
        # find the index of audio device
        found = -1
        for i in range(self.audio.get_device_count()):
            dev = self.audio.get_device_info_by_index(i)
            name = dev['name']#.encode('utf-8')
            print(i, name, dev['maxInputChannels'], dev['maxOutputChannels'])
            if name.find(device_name) >= 0 and dev['maxInputChannels'] > 0:
                found = i
                n_ch = int(dev['maxInputChannels'])
                break
        return found,n_ch

    def add_client(self, client_id, ip, port, channel):
        self.clients[client_id] = {'ip': ip, 'port': port, 'channel': channel}
        print(f"Added client {client_id} with IP {ip}, port {port}, channel {channel}")

    def update_client(self, client_id, ip=None, port=None, channel=None):
        if client_id in self.clients:
            if ip:
                self.clients[client_id]['ip'] = ip
            if port:
                self.clients[client_id]['port'] = port
            if channel:
                self.clients[client_id]['channel'] = channel
            print(f"Updated client {client_id} to IP {self.clients[client_id]['ip']}, port {self.clients[client_id]['port']}, channel {self.clients[client_id]['channel']}")

    def remove_client(self, client_id):
        if client_id in self.clients:
            del self.clients[client_id]
            print(f"Removed client {client_id}")

    def save_clients(self, filename):
        with open(filename, 'w') as file:
            json.dump(self.clients, file)
        print(f"Saved clients to {filename}")

    def load_clients(self, filename):
        try:
            with open(filename, 'r') as file:
                self.clients = json.load(file)
            print(f"Loaded clients from {filename}")
            return True
        except FileNotFoundError:
            print(f"File {filename} not found")
            return False

    def stream_audio(self):
        while self.is_streaming:
            data = self.stream.read(self.chunk_size, exception_on_overflow=False)
            for client_id, client_info in self.clients.items():
                channel_data = data[client_info['channel']::self.audio_channels]
                self.sock.sendto(channel_data, (client_info['ip'], client_info['port']))
 
    # pyaudio call back function to send packets
    def audio_callback(self,in_data, frame_count, time_info, status):
        data_array = np.frombuffer(in_data, dtype='int16')
        #for entity in CHANNEL_MAP:
        for client_id, client_info in self.clients.items():
            channel = data_array[client_info['channel']::self.audio_channels] # an error check here for ch<n_channels
            data_str = channel.tobytes()  # Use tobytes instead of tostring
            try:
                self.sock.sendto(data_str, (client_info['ip'], client_info['port']))   
            except OSError as error : 
                None
        return (in_data, pyaudio.paContinue)
    
    def start_streaming(self):
        if not self.is_streaming:
            self.is_streaming = True
            self.stream.start_stream() #
            #self.thread = threading.Thread(target=self.stream_audio)
            #self.thread.start()
            print("Audio streaming started")

    def stop_streaming(self):
        if self.is_streaming:
            self.is_streaming = False
            self.stream.stop_stream()
            #self.thread.join()
            print("Audio streaming stopped")

    def close(self):
        self.stop_streaming()
        self.stream.stop_stream()
        self.stream.close()
        self.audio.terminate()

class AudioStreamServerGUI(tk.Tk):
    def __init__(self, server):
        super().__init__()
        self.server = server
        self.title("Audio Stream Server")
        
        self.client_listbox = tk.Listbox(self, width=50, height=10)
        self.client_listbox.pack(pady=10)

        self.add_client_button = tk.Button(self, text="Add Client", command=self.add_client)
        self.add_client_button.pack(pady=5)
        
        self.update_client_button = tk.Button(self, text="Update Client", command=self.update_client)
        self.update_client_button.pack(pady=5)
        
        self.remove_client_button = tk.Button(self, text="Remove Client", command=self.remove_client)
        self.remove_client_button.pack(pady=5)

        self.save_clients_button = tk.Button(self, text="Save Clients", command=self.save_clients)
        self.save_clients_button.pack(pady=5)

        self.load_clients_button = tk.Button(self, text="Load Clients", command=self.load_clients)
        self.load_clients_button.pack(pady=5)
        
        self.start_button = tk.Button(self, text="Start Streaming", command=self.server.start_streaming)
        self.start_button.pack(pady=5)
        
        self.stop_button = tk.Button(self, text="Stop Streaming", command=self.server.stop_streaming)
        self.stop_button.pack(pady=5)

    def add_client(self):
        client_id = simpledialog.askstring("Client ID", "Enter client ID:")
        ip = simpledialog.askstring("IP Address", "Enter IP address:")
        port = simpledialog.askinteger("Port", "Enter port number:")
        channel = simpledialog.askinteger("Channel", "Enter channel number:")

        if client_id and ip and port is not None and channel is not None:
            self.server.add_client(client_id, ip, port, channel)
            self.client_listbox.insert(tk.END, f"{client_id} - {ip}:{port} - Channel {channel}")

    def update_client(self):
        selected = self.client_listbox.curselection()
        if selected:
            client_info = self.client_listbox.get(selected[0])
            client_id = client_info.split(" - ")[0]

            new_ip = simpledialog.askstring("New IP Address", "Enter new IP address:")
            new_port = simpledialog.askinteger("New Port", "Enter new port number:")
            new_channel = simpledialog.askinteger("New Channel", "Enter new channel number (0 or 1):")

            if new_ip and new_port is not None and new_channel is not None:
                self.server.update_client(client_id, ip=new_ip, port=new_port, channel=new_channel)
                self.client_listbox.delete(selected[0])
                self.client_listbox.insert(selected[0], f"{client_id} - {new_ip}:{new_port} - Channel {new_channel}")

    def remove_client(self):
        selected = self.client_listbox.curselection()
        if selected:
            client_info = self.client_listbox.get(selected[0])
            client_id = client_info.split(" - ")[0]
            self.server.remove_client(client_id)
            self.client_listbox.delete(selected[0])

    def save_clients(self):
        #filename = simpledialog.askstring("Save Clients", "Enter filename:")
        filename="artwalkClients"
        if filename:
            self.server.save_clients(filename)

    def load_clients(self):
        filename = simpledialog.askstring("Load Clients", "Enter filename:")
        if filename:
            if self.server.load_clients(filename):
                self.client_listbox.delete(0, tk.END)
                for client_id, client_info in self.server.clients.items():
                    self.client_listbox.insert(tk.END, f"{client_id} - {client_info['ip']}:{client_info['port']} - Channel {client_info['channel']}")

    def load_client_defaults(self):
        if self.server.load_clients("artwalkClients"):
            self.client_listbox.delete(0, tk.END)
            for client_id, client_info in self.server.clients.items():
                self.client_listbox.insert(tk.END, f"{client_id} - {client_info['ip']}:{client_info['port']} - Channel {client_info['channel']}")


    def on_closing(self):
        self.server.close()
        self.destroy()

if __name__ == "__main__":
    server = AudioStreamServer()
    gui = AudioStreamServerGUI(server)
    gui.load_client_defaults()
    gui.protocol("WM_DELETE_WINDOW", gui.on_closing)
    gui.mainloop()
