import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('127.0.0.1', 8080))
print("UDP Server running on port 8080...")
data, addr = s.recvfrom(1024)
print("Client says:", data.decode())
s.sendto(b"Hello client!", addr)
s.close()
