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
	setup_listener ${p}
done

for p in $(seq 0 ${NUM_PEERS}); do
	setup_ns ${p} 5.5.5.$((${p} + 1))/24 ${MTU}
done

for p in $(seq 0 ${NUM_PEERS}); do
	add_peer ${p}
done

for p in $(seq 1 ${NUM_PEERS}); do
	ip netns exec peer0 ${OVPN_CLI} set_peer tun0 ${p} 60 120
	ip netns exec peer${p} ${OVPN_CLI} set_peer tun${p} \
		$((${p}+ID_OFFSET)) 60 120
done

sleep 1

TCPDUMP_TIMEOUT="1.5s"
for p in $(seq 1 ${NUM_PEERS}); do
	# The first part of the data packet header consists of:
	# - TCP only: 2 bytes for the packet length
	# - 5 bits for opcode ("9" for DATA_V2)
	# - 3 bits for key-id ("0" at this point)
	# - 12 bytes for peer-id:
	#     - with asymmetric ID: "${p}" one way and "${p} + 9" the other way
	#     - with symmetric ID: "${p}" both ways
	HEADER1=$(printf "0x4800000%x" ${p})
	HEADER2=$(printf "0x4800000%x" $((${p} + ID_OFFSET)))
	RADDR=""
	if [ "${PROTO}" == "UDP" ]; then
		RADDR=$(awk "NR == ${p} {print \$3}" ${UDP_PEERS_FILE})
	fi

	timeout ${TCPDUMP_TIMEOUT} ip netns exec peer${p} \
		tcpdump --immediate-mode -p -ni veth${p} -c 1 \
		"$(build_capture_filter "${HEADER1}" "${RADDR}")" \
		>/dev/null 2>&1 &
	TCPDUMP_PID1=$!
	timeout ${TCPDUMP_TIMEOUT} ip netns exec peer${p} \
		tcpdump --immediate-mode -p -ni veth${p} -c 1 \
		"$(build_capture_filter "${HEADER2}" "${RADDR}")" \
		>/dev/null 2>&1 &
	TCPDUMP_PID2=$!

	sleep 0.3
	ip netns exec peer0 ping -qfc 500 -w 3 5.5.5.$((${p} + 1))
	ip netns exec peer0 ping -qfc 500 -s 3000 -w 3 5.5.5.$((${p} + 1))

	wait ${TCPDUMP_PID1}
	wait ${TCPDUMP_PID2}
done

# ping LAN behind client 1
ip netns exec peer0 ping -qfc 500 -w 3 ${LAN_IP}

if [ "$FLOAT" == "1" ]; then
	# make clients float..
	for p in $(seq 1 ${NUM_PEERS}); do
		ip -n peer${p} addr del 10.10.${p}.2/24 dev veth${p}
		ip -n peer${p} addr add 10.10.${p}.3/24 dev veth${p}
	done
	for p in $(seq 1 ${NUM_PEERS}); do
		ip netns exec peer${p} ping -qfc 500 -w 3 5.5.5.1
	done
fi

ip netns exec peer0 iperf3 -1 -s &
sleep 1
ip netns exec peer1 iperf3 -Z -t 3 -c 5.5.5.1

echo "Adding secondary key and then swap:"
for p in $(seq 1 ${NUM_PEERS}); do
	ip netns exec peer0 ${OVPN_CLI} new_key tun0 ${p} 2 1 ${ALG} 0 \
		data64.key
	ip netns exec peer${p} ${OVPN_CLI} new_key tun${p} \
		$((${p} + ID_OFFSET)) 2 1 ${ALG} 1 data64.key
	ip netns exec peer${p} ${OVPN_CLI} swap_keys tun${p} \
		$((${p} + ID_OFFSET))
done

sleep 1

echo "Querying all peers:"
ip netns exec peer0 ${OVPN_CLI} get_peer tun0
ip netns exec peer1 ${OVPN_CLI} get_peer tun1

echo "Querying peer 1:"
ip netns exec peer0 ${OVPN_CLI} get_peer tun0 1

echo "Querying non-existent peer 20:"
ip netns exec peer0 ${OVPN_CLI} get_peer tun0 20 || true

echo "Deleting peer 1:"
ip netns exec peer0 ${OVPN_CLI} del_peer tun0 1
ip netns exec peer1 ${OVPN_CLI} del_peer tun1 $((1 + ID_OFFSET))

echo "Querying keys:"
for p in $(seq 2 ${NUM_PEERS}); do
	ip netns exec peer${p} ${OVPN_CLI} get_key tun${p} \
		$((${p} + ID_OFFSET)) 1
	ip netns exec peer${p} ${OVPN_CLI} get_key tun${p} \
		$((${p} + ID_OFFSET)) 2
done

echo "Deleting peer while sending traffic:"
(ip netns exec peer2 ping -qf -w 4 5.5.5.1)&
sleep 2
ip netns exec peer0 ${OVPN_CLI} del_peer tun0 2
# following command fails in TCP mode
# (both ends get conn reset when one peer disconnects)
ip netns exec peer2 ${OVPN_CLI} del_peer tun2 $((2 + ID_OFFSET)) || true

echo "Deleting keys:"
for p in $(seq 3 ${NUM_PEERS}); do
	ip netns exec peer${p} ${OVPN_CLI} del_key tun${p} \
		$((${p} + ID_OFFSET)) 1
	ip netns exec peer${p} ${OVPN_CLI} del_key tun${p} \
		$((${p} + ID_OFFSET)) 2
done

echo "Setting timeout to 3s MP:"
for p in $(seq 3 ${NUM_PEERS}); do
	ip netns exec peer0 ${OVPN_CLI} set_peer tun0 ${p} 3 3 || true
	ip netns exec peer${p} ${OVPN_CLI} set_peer tun${p} \
		$((${p} + ID_OFFSET)) 0 0
done
# wait for peers to timeout
sleep 5

echo "Setting timeout to 3s P2P:"
for p in $(seq 3 ${NUM_PEERS}); do
	ip netns exec peer${p} ${OVPN_CLI} set_peer tun${p} \
		$((${p} + ID_OFFSET)) 3 3
done
sleep 5

for p in $(seq 0 ${NUM_PEERS}); do
	compare_ntfs ${p}
done

cleanup

modprobe -r ovpn || true
