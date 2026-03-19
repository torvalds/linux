#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020-2025 OpenVPN, Inc.
#
#  Author:	Antonio Quartulli <antonio@openvpn.net>

UDP_PEERS_FILE=${UDP_PEERS_FILE:-udp_peers.txt}
TCP_PEERS_FILE=${TCP_PEERS_FILE:-tcp_peers.txt}
OVPN_CLI=${OVPN_CLI:-./ovpn-cli}
YNL_CLI=${YNL_CLI:-../../../../net/ynl/pyynl/cli.py}
ALG=${ALG:-aes}
PROTO=${PROTO:-UDP}
FLOAT=${FLOAT:-0}
SYMMETRIC_ID=${SYMMETRIC_ID:-0}

export ID_OFFSET=$(( 9 * (SYMMETRIC_ID == 0) ))

JQ_FILTER='map(select(.msg.peer | has("remote-ipv6") | not)) |
	map(del(.msg.ifindex)) | sort_by(.msg.peer.id)[]'
LAN_IP="11.11.11.11"

declare -A tmp_jsons=()
declare -A listener_pids=()

create_ns() {
	ip netns add peer${1}
}

setup_ns() {
	MODE="P2P"

	if [ ${1} -eq 0 ]; then
		MODE="MP"
		for p in $(seq 1 ${NUM_PEERS}); do
			ip link add veth${p} netns peer0 type veth peer name veth${p} netns peer${p}

			ip -n peer0 addr add 10.10.${p}.1/24 dev veth${p}
			ip -n peer0 addr add fd00:0:0:${p}::1/64 dev veth${p}
			ip -n peer0 link set veth${p} up

			ip -n peer${p} addr add 10.10.${p}.2/24 dev veth${p}
			ip -n peer${p} addr add fd00:0:0:${p}::2/64 dev veth${p}
			ip -n peer${p} link set veth${p} up
		done
	fi

	ip netns exec peer${1} ${OVPN_CLI} new_iface tun${1} $MODE
	ip -n peer${1} addr add ${2} dev tun${1}
	# add a secondary IP to peer 1, to test a LAN behind a client
	if [ ${1} -eq 1 -a -n "${LAN_IP}" ]; then
		ip -n peer${1} addr add ${LAN_IP} dev tun${1}
		ip -n peer0 route add ${LAN_IP} via $(echo ${2} |sed -e s'!/.*!!') dev tun0
	fi
	if [ -n "${3}" ]; then
		ip -n peer${1} link set mtu ${3} dev tun${1}
	fi
	ip -n peer${1} link set tun${1} up
}

build_capture_filter() {
	# match the first four bytes of the openvpn data payload
	if [ "${PROTO}" == "UDP" ]; then
		# For UDP, libpcap transport indexing only works for IPv4, so
		# use an explicit IPv4 or IPv6 expression based on the peer
		# address. The IPv6 branch assumes there are no extension
		# headers in the outer packet.
		if [[ "${2}" == *:* ]]; then
			printf "ip6 and ip6[6] = 17 and ip6[48:4] = %s" "${1}"
		else
			printf "ip and udp[8:4] = %s" "${1}"
		fi
	else
		# openvpn over TCP prepends a 2-byte packet length ahead of the
		# DATA_V2 opcode, so skip it before matching the payload header
		printf "ip and tcp[(((tcp[12] & 0xf0) >> 2) + 2):4] = %s" "${1}"
	fi
}

setup_listener() {
	file=$(mktemp)
	PYTHONUNBUFFERED=1 ip netns exec peer${p} ${YNL_CLI} --family ovpn \
		--subscribe peers --output-json --duration 40 > ${file} &
	listener_pids[$1]=$!
	tmp_jsons[$1]="${file}"
}

add_peer() {
	labels=("ASYMM" "SYMM")
	M_ID=${labels[SYMMETRIC_ID]}

	if [ "${PROTO}" == "UDP" ]; then
		if [ ${1} -eq 0 ]; then
			ip netns exec peer0 ${OVPN_CLI} new_multi_peer tun0 1 \
				${M_ID} ${UDP_PEERS_FILE}

			for p in $(seq 1 ${NUM_PEERS}); do
				ip netns exec peer0 ${OVPN_CLI} new_key tun0 ${p} 1 0 ${ALG} 0 \
					data64.key
			done
		else
			if [ "${SYMMETRIC_ID}" -eq 1 ]; then
				PEER_ID=${1}
				TX_ID="none"
			else
				PEER_ID=$(awk "NR == ${1} {print \$2}" \
					${UDP_PEERS_FILE})
				TX_ID=${1}
			fi
			RADDR=$(awk "NR == ${1} {print \$3}" ${UDP_PEERS_FILE})
			RPORT=$(awk "NR == ${1} {print \$4}" ${UDP_PEERS_FILE})
			LPORT=$(awk "NR == ${1} {print \$6}" ${UDP_PEERS_FILE})
			ip netns exec peer${1} ${OVPN_CLI} new_peer tun${1} \
				${PEER_ID} ${TX_ID} ${LPORT} ${RADDR} ${RPORT}
			ip netns exec peer${1} ${OVPN_CLI} new_key tun${1} \
				${PEER_ID} 1 0 ${ALG} 1 data64.key
		fi
	else
		if [ ${1} -eq 0 ]; then
			(ip netns exec peer0 ${OVPN_CLI} listen tun0 1 ${M_ID} \
				${TCP_PEERS_FILE} && {
				for p in $(seq 1 ${NUM_PEERS}); do
					ip netns exec peer0 ${OVPN_CLI} new_key tun0 ${p} 1 0 \
						${ALG} 0 data64.key
				done
			}) &
			sleep 5
		else
			if [ "${SYMMETRIC_ID}" -eq 1 ]; then
				PEER_ID=${1}
				TX_ID="none"
			else
				PEER_ID=$(awk "NR == ${1} {print \$2}" \
					${TCP_PEERS_FILE})
				TX_ID=${1}
			fi
			ip netns exec peer${1} ${OVPN_CLI} connect tun${1} \
				${PEER_ID} ${TX_ID} 10.10.${1}.1 1 data64.key
		fi
	fi
}

compare_ntfs() {
	if [ ${#tmp_jsons[@]} -gt 0 ]; then
		suffix=""
		[ "${SYMMETRIC_ID}" -eq 1 ] && suffix="${suffix}-symm"
		[ "$FLOAT" == 1 ] && suffix="${suffix}-float"
		expected="json/peer${1}${suffix}.json"
		received="${tmp_jsons[$1]}"

		kill -TERM ${listener_pids[$1]} || true
		wait ${listener_pids[$1]} || true
		printf "Checking notifications for peer ${1}... "
		if diff <(jq -s "${JQ_FILTER}" ${expected}) \
			<(jq -s "${JQ_FILTER}" ${received}); then
			echo "OK"
		fi

		rm -f ${received} || true
	fi
}

cleanup() {
	# some ovpn-cli processes sleep in background so they need manual poking
	killall $(basename ${OVPN_CLI}) 2>/dev/null || true

	# netns peer0 is deleted without erasing ifaces first
	for p in $(seq 1 10); do
		ip -n peer${p} link set tun${p} down 2>/dev/null || true
		ip netns exec peer${p} ${OVPN_CLI} del_iface tun${p} 2>/dev/null || true
	done
	for p in $(seq 1 10); do
		ip -n peer0 link del veth${p} 2>/dev/null || true
	done
	for p in $(seq 0 10); do
		ip netns del peer${p} 2>/dev/null || true
	done
}

if [ "${PROTO}" == "UDP" ]; then
	NUM_PEERS=${NUM_PEERS:-$(wc -l ${UDP_PEERS_FILE} | awk '{print $1}')}
else
	NUM_PEERS=${NUM_PEERS:-$(wc -l ${TCP_PEERS_FILE} | awk '{print $1}')}
fi
