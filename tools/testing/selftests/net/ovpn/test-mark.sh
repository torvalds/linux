#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020-2025 OpenVPN, Inc.
#
#	Author:	Ralf Lici <ralf@mandelbit.com>
#		Antonio Quartulli <antonio@openvpn.net>

#set -x
set -e

MARK=1056

source ./common.sh

cleanup

modprobe -q ovpn || true

for p in $(seq 0 "${NUM_PEERS}"); do
	create_ns "${p}"
done

for p in $(seq 0 3); do
	setup_ns "${p}" 5.5.5.$((p + 1))/24
done

# add peer0 with mark
ip netns exec peer0 "${OVPN_CLI}" new_multi_peer tun0 1 ASYMM \
	"${UDP_PEERS_FILE}" \
	${MARK}
for p in $(seq 1 3); do
	ip netns exec peer0 "${OVPN_CLI}" new_key tun0 "${p}" 1 0 "${ALG}" 0 \
		data64.key
done

for p in $(seq 1 3); do
	add_peer "${p}"
done

for p in $(seq 1 3); do
	ip netns exec peer0 "${OVPN_CLI}" set_peer tun0 "${p}" 60 120
	ip netns exec peer"${p}" "${OVPN_CLI}" set_peer tun"${p}" \
		$((p + 9)) 60 120
done

sleep 1

for p in $(seq 1 3); do
	ip netns exec peer0 ping -qfc 500 -w 3 5.5.5.$((p + 1))
done

echo "Adding an nftables drop rule based on mark value ${MARK}"
ip netns exec peer0 nft flush ruleset
ip netns exec peer0 nft 'add table inet filter'
ip netns exec peer0 nft 'add chain inet filter output {
	type filter hook output priority 0;
	policy accept;
}'
ip netns exec peer0 nft add rule inet filter output \
	meta mark == ${MARK} \
	counter drop

DROP_COUNTER=$(ip netns exec peer0 nft list chain inet filter output \
	| sed -n 's/.*packets \([0-9]*\).*/\1/p')
sleep 1

# ping should fail
for p in $(seq 1 3); do
	PING_OUTPUT=$(ip netns exec peer0 ping \
		-qfc 500 -w 1 5.5.5.$((p + 1)) 2>&1) && exit 1
	echo "${PING_OUTPUT}"
	LOST_PACKETS=$(echo "$PING_OUTPUT" \
		| awk '/packets transmitted/ { print $1 }')
	# increment the drop counter by the amount of lost packets
	DROP_COUNTER=$((DROP_COUNTER + LOST_PACKETS))
done

# check if the final nft counter matches our counter
TOTAL_COUNT=$(ip netns exec peer0 nft list chain inet filter output \
	| sed -n 's/.*packets \([0-9]*\).*/\1/p')
if [ "${DROP_COUNTER}" -ne "${TOTAL_COUNT}" ]; then
	echo "Expected ${TOTAL_COUNT} drops, got ${DROP_COUNTER}"
	exit 1
fi

echo "Removing the drop rule"
ip netns exec peer0 nft flush ruleset
sleep 1

for p in $(seq 1 3); do
	ip netns exec peer0 ping -qfc 500 -w 3 5.5.5.$((p + 1))
done

cleanup

modprobe -r ovpn || true
