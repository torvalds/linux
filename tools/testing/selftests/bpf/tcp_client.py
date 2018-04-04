#!/usr/bin/env python2
#
# SPDX-License-Identifier: GPL-2.0
#

import sys, os, os.path, getopt
import socket, time
import subprocess
import select

def read(sock, n):
    buf = ''
    while len(buf) < n:
        rem = n - len(buf)
        try: s = sock.recv(rem)
        except (socket.error), e: return ''
        buf += s
    return buf

def send(sock, s):
    total = len(s)
    count = 0
    while count < total:
        try: n = sock.send(s)
        except (socket.error), e: n = 0
        if n == 0:
            return count;
        count += n
    return count


serverPort = int(sys.argv[1])
HostName = socket.gethostname()

# create active socket
sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
try:
    sock.connect((HostName, serverPort))
except socket.error as e:
    sys.exit(1)

buf = ''
n = 0
while n < 1000:
    buf += '+'
    n += 1

sock.settimeout(1);
n = send(sock, buf)
n = read(sock, 500)
sys.exit(0)
