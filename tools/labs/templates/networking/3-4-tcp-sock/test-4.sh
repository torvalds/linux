#!/bin/sh
#
# SO2 - Networking Lab (#10)
#
# Test script for exercise #3
#

set -x

# insert module (run in background, it waits for a connection)
insmod tcp_sock.ko &

# wait for module to start listening
sleep 1

# list all currently listening servers and active connections
# for both TCP and UDP, and don't resolve hostnames
netstat -tuan

# connect to localhost, port 60000, starting a connection using local
# port number 600001;
echo "Should connect." | ../netcat -q 4 127.0.0.1 60000 -p 60001 &

# wait for connection to be established then remove module
# (and close connection)
sleep 3

# remove module
rmmod tcp_sock || exit 1
