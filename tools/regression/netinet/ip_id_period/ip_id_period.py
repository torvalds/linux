# Copyright (C) 2008 Michael J. Silbersack.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice unmodified, this list of conditions, and the following
#    disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#
# This is a regression test to verify the proper behavior of IP ID generation
# code.  It will push 200000 packets, then report back what the min and max
# periods it saw for different IDs were.

from __future__ import print_function
import os
import signal
import subprocess
import time

if os.path.exists('results.pcap'):
    os.remove('results.pcap')
tcpdump = subprocess.Popen('tcpdump -n -i lo0 -w results.pcap icmp', shell=True)
time.sleep(1) # Give tcpdump time to start

os.system('sysctl net.inet.icmp.icmplim=0')
os.system('ping -q -i .001 -c 100000 127.0.0.1')

time.sleep(3) # Give tcpdump time to catch up
os.kill(tcpdump.pid, signal.SIGTERM)

os.system('tcpdump -n -v -r results.pcap > results.txt')

id_lastseen = {}
id_minperiod = {}

count = 0
for line in open('results.txt').readlines():
    id = int(line.split(' id ')[1].split(',')[0])
    if id in id_lastseen:
        period = count - id_lastseen[id]
        if id not in id_minperiod or period < id_minperiod[id]:
            id_minperiod[id] = period
    id_lastseen[id] = count
    count += 1

sorted_minperiod = list(zip(*reversed(list(zip(*list(id_minperiod.items()))))))
sorted_minperiod.sort()

print("Lowest 10 ID periods detected:")
x = 0
while x < 10:
    id_tuple = sorted_minperiod.pop(0)
    print("id: %d period: %d" % (id_tuple[1], id_tuple[0]))
    x += 1

print("Highest 10 ID periods detected:")
x = 0
while x < 10:
    id_tuple = sorted_minperiod.pop()
    print("id: %d period: %d" % (id_tuple[1], id_tuple[0]))
    x += 1
