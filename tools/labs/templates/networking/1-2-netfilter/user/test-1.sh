#!/bin/sh
#
# SO2 - Networking Lab (#10)
#
# Test script for exercise #1
#

# insert module
insmod ../kernel/filter.ko || exit 1

# listen for connections on localhost, port 60000 (run in background)
../../netcat -l -p 60000 &

# wait for netcat to start listening
sleep 1

# connect to localhost, port 60000, starting a connection using local
# port number 600001;
echo "Should show up in filter." | ../../netcat -q 2 127.0.0.1 60000

# look for filter message in dmesg output
echo "Check dmesg output."

# remove module
rmmod filter || exit 1
