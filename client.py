import socket
s=socket.socket()
s.connect(('127.0.0.1',8080))
s.send(b"Hello server!")
msg=s.recv(1024).decode()
print("Server says:",msg)
s.close()