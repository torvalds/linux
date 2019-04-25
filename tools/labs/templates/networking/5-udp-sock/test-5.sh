#!/bin/sh
#
# SO2 - Networking Lab (#10)
#
# Test script for bonus exercise
#

set -x

# listen for UDP packets on localhost, port 60001 (run in background)
../netcat -l -u -p 60001 &

# get pid of netcat
pid=$!

# wait for netcat to start listening
sleep 1

# insert module, causing the message to be sent
insmod udp_sock.ko

# remove module
rmmod udp_sock

# kill netcat
kill $pid 2>/dev/null
