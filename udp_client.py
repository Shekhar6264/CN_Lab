import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.sendto(b"Hello server!", ('127.0.0.1', 8080))
data, addr = s.recvfrom(1024)
print("Server says:", data.decode())
s.close()
