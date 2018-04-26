#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Run a series of udpgso regression tests

echo "ipv4 cmsg"
./in_netns.sh ./udpgso -4 -C

echo "ipv4 setsockopt"
./in_netns.sh ./udpgso -4 -C -s

echo "ipv6 cmsg"
./in_netns.sh ./udpgso -6 -C

echo "ipv6 setsockopt"
./in_netns.sh ./udpgso -6 -C -s
