#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0
#

import sys, os, os.path, getopt
import socket, time
import subprocess
import select

def read(sock, n):
    buf = b''
    while len(buf) < n:
        rem = n - len(buf)
        try: s = sock.recv(rem)
        except (socket.error) as e: return b''
        buf += s
    return buf

def send(sock, s):
    total = len(s)
    count = 0
    while count < total:
        try: n = sock.send(s)
        except (socket.error) as e: n = 0
        if n == 0:
            return count;
        count += n
    return count


SERVER_PORT = 12877
MAX_PORTS = 2

serverPort = SERVER_PORT
serverSocket = None

HostName = socket.gethostname()

# create passive socket
serverSocket = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
host = socket.gethostname()

try: serverSocket.bind((host, 0))
except socket.error as msg:
    print('bind fails: ' + str(msg))

sn = serverSocket.getsockname()
serverPort = sn[1]

cmdStr = ("./tcp_client.py %d &") % (serverPort)
os.system(cmdStr)

buf = b''
n = 0
while n < 500:
    buf += b'.'
    n += 1

serverSocket.listen(MAX_PORTS)
readList = [serverSocket]

while True:
    readyRead, readyWrite, inError = \
        select.select(readList, [], [], 2)

    if len(readyRead) > 0:
        waitCount = 0
        for sock in readyRead:
            if sock == serverSocket:
                (clientSocket, address) = serverSocket.accept()
                address = str(address[0])
                readList.append(clientSocket)
            else:
                sock.settimeout(1);
                s = read(sock, 1000)
                n = send(sock, buf)
                sock.close()
                serverSocket.close()
                sys.exit(0)
    else:
        print('Select timeout!')
        sys.exit(1)
