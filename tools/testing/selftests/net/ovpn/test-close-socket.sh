#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020-2025 OpenVPN, Inc.
#
#  Author:	Antonio Quartulli <antonio@openvpn.net>

#set -x
set -e

source ./common.sh

cleanup

modprobe -q ovpn || true

for p in $(seq 0 ${NUM_PEERS}); do
	create_ns ${p}
done

for p in $(seq 0 ${NUM_PEERS}); do
	setup_ns ${p} 5.5.5.$((${p} + 1))/24
done

for p in $(seq 0 ${NUM_PEERS}); do
	add_peer ${p}
done

for p in $(seq 1 ${NUM_PEERS}); do
	ip netns exec peer0 ${OVPN_CLI} set_peer tun0 ${p} 60 120
	ip netns exec peer${p} ${OVPN_CLI} set_peer tun${p} ${p} 60 120
done

sleep 1

for p in $(seq 1 ${NUM_PEERS}); do
	ip netns exec peer0 ping -qfc 500 -w 3 5.5.5.$((${p} + 1))
done

ip netns exec peer0 iperf3 -1 -s &
sleep 1
ip netns exec peer1 iperf3 -Z -t 3 -c 5.5.5.1

cleanup

modprobe -r ovpn || true
