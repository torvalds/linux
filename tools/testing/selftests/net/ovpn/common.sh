#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020-2025 OpenVPN, Inc.
#
#  Author:	Antonio Quartulli <antonio@openvpn.net>

OVPN_COMMON_DIR=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
source "$OVPN_COMMON_DIR/../../kselftest/ktap_helpers.sh"

OVPN_UDP_PEERS_FILE=${OVPN_UDP_PEERS_FILE:-udp_peers.txt}
OVPN_TCP_PEERS_FILE=${OVPN_TCP_PEERS_FILE:-tcp_peers.txt}
OVPN_CLI=${OVPN_CLI:-${OVPN_COMMON_DIR}/ovpn-cli}
OVPN_YNL=${OVPN_YNL:-${OVPN_COMMON_DIR}/../../../../net/ynl/pyynl/cli.py}
OVPN_ALG=${OVPN_ALG:-aes}
OVPN_PROTO=${OVPN_PROTO:-UDP}
OVPN_FLOAT=${OVPN_FLOAT:-0}
OVPN_SYMMETRIC_ID=${OVPN_SYMMETRIC_ID:-0}
OVPN_VERBOSE=${OVPN_VERBOSE:-0}

export OVPN_ID_OFFSET=$(( 9 * (OVPN_SYMMETRIC_ID == 0) ))

OVPN_JQ_FILTER='map(if type == "array" then .[] else . end) |
	map(select(.msg.peer | has("remote-ipv6") | not)) |
	map(del(.msg.ifindex)) | sort_by(.msg.peer.id)[]'
OVPN_LAN_IP="11.11.11.11"

declare -A OVPN_TMP_JSONS=()
declare -A OVPN_LISTENER_PIDS=()
OVPN_CURRENT_STAGE=""

ovpn_is_verbose() {
	[[ "${OVPN_VERBOSE}" == "1" ]]
}

ovpn_log() {
	ovpn_is_verbose || return 0
	printf '%s\n' "$*"
}

ovpn_print_cmd_output() {
	local output_file="$1"
	local line

	[[ -s "${output_file}" ]] || return 0

	while IFS= read -r line; do
		ovpn_log "${line}"
	done < "${output_file}"
}

ovpn_cmd_run() {
	local mode="$1"
	local label="$2"
	local output_file
	local rc
	local ret=0

	shift 2

	output_file=$(mktemp)
	if "$@" >"${output_file}" 2>&1; then
		rc=0
	else
		rc=$?
	fi

	case "${mode}" in
	ok)
		if [[ "${rc}" -ne 0 ]]; then
			cat "${output_file}"
			printf '%s\n' \
				"${label}: command failed with rc=${rc}: $*"
			ret="${rc}"
		fi
		;;
	mayfail)
		;;
	fail)
		[[ "${rc}" -eq 0 ]] && ret=1
		;;
	esac

	if ovpn_is_verbose && [[ "${rc}" -eq 0 || "${mode}" != "ok" ]]; then
		ovpn_print_cmd_output "${output_file}"
	fi

	rm -f "${output_file}"
	return "${ret}"
}

ovpn_cmd_ok() {
	ovpn_cmd_run ok "$@"
}

ovpn_cmd_mayfail() {
	ovpn_cmd_run mayfail "$@"
}

ovpn_cmd_fail() {
	ovpn_cmd_run fail "$@"
}

ovpn_run_bg() {
	local pid_var="$1"

	shift
	if ovpn_is_verbose; then
		"$@" &
	else
		"$@" >/dev/null 2>&1 &
	fi

	printf -v "${pid_var}" '%s' "$!"
}

ovpn_run_stage() {
	local label="$1"

	shift
	OVPN_CURRENT_STAGE="${label}"
	"$@"
	OVPN_CURRENT_STAGE=""
	ktap_test_pass "${label}"
}

ovpn_stage_err() {
	# ERR trap is global under set -eE: only report failures that happen
	# while ovpn_run_stage() is actively executing a stage body.
	if [[ -n "${OVPN_CURRENT_STAGE}" ]]; then
		ktap_test_fail "${OVPN_CURRENT_STAGE}"
		OVPN_CURRENT_STAGE=""
	fi
}

ovpn_create_ns() {
	ip netns add "ovpn_peer${1}"
}

ovpn_peer_vpn_addr() {
	local peer="$1"
	local file

	if [ "${OVPN_PROTO}" == "UDP" ]; then
		file="${OVPN_UDP_PEERS_FILE}"
	else
		file="${OVPN_TCP_PEERS_FILE}"
	fi

	awk -v peer="${peer}" '$1 == peer {print $NF; exit}' "${file}"
}

ovpn_setup_ns() {
	local peer="ovpn_peer${1}"
	local server_ns="ovpn_peer0"
	local peer_ns
	MODE="P2P"

	if [ ${1} -eq 0 ]; then
		MODE="MP"
		for p in $(seq 1 ${OVPN_NUM_PEERS}); do
			peer_ns="ovpn_peer${p}"
			ip link add veth${p} netns "${server_ns}" type veth \
				peer name veth${p} netns "${peer_ns}"

			ip -n "${server_ns}" addr add 10.10.${p}.1/24 dev \
				veth${p}
			ip -n "${server_ns}" addr add fd00:0:0:${p}::1/64 dev \
				veth${p}
			ip -n "${server_ns}" link set veth${p} up

			ip -n "${peer_ns}" addr add 10.10.${p}.2/24 dev veth${p}
			ip -n "${peer_ns}" addr add fd00:0:0:${p}::2/64 dev \
				veth${p}
			ip -n "${peer_ns}" link set veth${p} up
		done
	fi

	ip netns exec "${peer}" ${OVPN_CLI} new_iface tun${1} $MODE
	ip -n "${peer}" addr add ${2} dev tun${1}
	# add a secondary IP to peer 1, to test a LAN behind a client
	if [ ${1} -eq 1 -a -n "${OVPN_LAN_IP}" ]; then
		ip -n "${peer}" addr add ${OVPN_LAN_IP} dev tun${1}
		ip -n "${server_ns}" route add ${OVPN_LAN_IP} via \
			$(echo ${2} |sed -e s'!/.*!!') dev tun0
	fi
	if [ -n "${3}" ]; then
		ip -n "${peer}" link set mtu ${3} dev tun${1}
	fi
	ip -n "${peer}" link set tun${1} up
}

ovpn_build_capture_filter() {
	# match the first four bytes of the openvpn data payload
	if [ "${OVPN_PROTO}" == "UDP" ]; then
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

ovpn_setup_listener() {
	local peer="$1"
	local file
	local peer_ns="ovpn_peer${peer}"

	file=$(mktemp)
	PYTHONUNBUFFERED=1 ip netns exec "${peer_ns}" "${OVPN_YNL}" --family \
		ovpn --subscribe peers --output-json > "${file}" \
		2>/dev/null &
	OVPN_LISTENER_PIDS["${peer}"]=$!
	OVPN_TMP_JSONS["${peer}"]="${file}"
}

ovpn_add_peer() {
	labels=("ASYMM" "SYMM")
	local peer_ns
	local server_ns="ovpn_peer0"
	M_ID=${labels[OVPN_SYMMETRIC_ID]}

	if [ "${OVPN_PROTO}" == "UDP" ]; then
		if [ ${1} -eq 0 ]; then
			ip netns exec "${server_ns}" ${OVPN_CLI} \
				new_multi_peer tun0 1 ${M_ID} \
				${OVPN_UDP_PEERS_FILE}

			for p in $(seq 1 ${OVPN_NUM_PEERS}); do
				ip netns exec "${server_ns}" ${OVPN_CLI} \
					new_key tun0 ${p} 1 0 ${OVPN_ALG} 0 \
					data64.key
			done
		else
			peer_ns="ovpn_peer${1}"
			if [ "${OVPN_SYMMETRIC_ID}" -eq 1 ]; then
				PEER_ID=${1}
				TX_ID="none"
			else
				PEER_ID=$(awk "NR == ${1} {print \$2}" \
					${OVPN_UDP_PEERS_FILE})
				TX_ID=${1}
			fi
			RADDR=$(awk "NR == ${1} {print \$3}" \
				${OVPN_UDP_PEERS_FILE})
			RPORT=$(awk "NR == ${1} {print \$4}" \
				${OVPN_UDP_PEERS_FILE})
			LPORT=$(awk "NR == ${1} {print \$6}" \
				${OVPN_UDP_PEERS_FILE})
			ip netns exec "${peer_ns}" ${OVPN_CLI} new_peer \
				tun${1} ${PEER_ID} ${TX_ID} ${LPORT} ${RADDR} \
				${RPORT}
			ip netns exec "${peer_ns}" ${OVPN_CLI} new_key tun${1} \
				${PEER_ID} 1 0 ${OVPN_ALG} 1 data64.key
		fi
	else
		if [ ${1} -eq 0 ]; then
			(ip netns exec "${server_ns}" ${OVPN_CLI} listen tun0 \
				1 ${M_ID} ${OVPN_TCP_PEERS_FILE} && {
				for p in $(seq 1 ${OVPN_NUM_PEERS}); do
					ip netns exec "${server_ns}" \
						${OVPN_CLI} new_key tun0 ${p} \
						1 0 ${OVPN_ALG} 0 data64.key
				done
			}) &
			sleep 5
		else
			peer_ns="ovpn_peer${1}"
			if [ "${OVPN_SYMMETRIC_ID}" -eq 1 ]; then
				PEER_ID=${1}
				TX_ID="none"
			else
				PEER_ID=$(awk "NR == ${1} {print \$2}" \
					${OVPN_TCP_PEERS_FILE})
				TX_ID=${1}
			fi
			ip netns exec "${peer_ns}" ${OVPN_CLI} connect tun${1} \
				${PEER_ID} ${TX_ID} 10.10.${1}.1 1 data64.key
		fi
	fi
}

ovpn_compare_ntfs() {
	local diff_rc=0
	local diff_file

	if [ ${#OVPN_TMP_JSONS[@]} -gt 0 ]; then
		suffix=""
		[ "${OVPN_SYMMETRIC_ID}" -eq 1 ] && suffix="${suffix}-symm"
		[ "$OVPN_FLOAT" == 1 ] && suffix="${suffix}-float"
		expected="json/peer${1}${suffix}.json"
		received="${OVPN_TMP_JSONS[$1]}"
		diff_file=$(mktemp)

		ovpn_stop_listener "${1}" 1
		printf "Checking notifications for peer ${1}... "
		if diff <(jq -s "${OVPN_JQ_FILTER}" ${expected}) \
			<(jq -s "${OVPN_JQ_FILTER}" ${received}) \
			>"${diff_file}" 2>&1; then
			echo "OK"
		else
			diff_rc=$?
			echo "failed"
			cat "${diff_file}"
		fi

		rm -f "${diff_file}" || true
		rm -f "${received}" || true
		unset "OVPN_TMP_JSONS[$1]"
	fi

	return "${diff_rc}"
}

ovpn_stop_listener() {
	local peer="$1"
	local keep_json="${2:-0}"
	local pid="${OVPN_LISTENER_PIDS[$peer]:-}"
	local json="${OVPN_TMP_JSONS[$peer]:-}"

	if [[ -n "${pid}" ]]; then
		kill -TERM "${pid}" 2>/dev/null || true
		wait "${pid}" 2>/dev/null || true
		unset "OVPN_LISTENER_PIDS[$peer]"
	fi

	if [[ -n "${json}" && "${keep_json}" -eq 0 ]]; then
		rm -f "${json}" || true
		unset "OVPN_TMP_JSONS[$peer]"
	fi
}

ovpn_cleanup_peer_ns() {
	local peer="$1"
	local peer_id="${peer#ovpn_peer}"

	ip -n "${peer}" link set tun${peer_id} down 2>/dev/null || true
	ip netns exec "${peer}" ${OVPN_CLI} del_iface tun${peer_id} \
		1>/dev/null 2>&1 || true
	ip netns del "${peer}" 2>/dev/null || true
}

ovpn_cleanup() {
	local peer

	# some ovpn-cli processes sleep in background so they need manual poking
	killall "$(basename "${OVPN_CLI}")" 2>/dev/null || true

	for peer in "${!OVPN_LISTENER_PIDS[@]}"; do
		ovpn_stop_listener "${peer}" 2>/dev/null
	done

	for p in $(seq 1 10); do
		ip -n ovpn_peer0 link del veth${p} 2>/dev/null || true
	done

	# remove from ovpn's netns pool
	while IFS= read -r peer; do
		[[ -n "${peer}" ]] || continue
		ovpn_cleanup_peer_ns "${peer}" 2>/dev/null
	done < <(ip netns list 2>/dev/null | awk '/^ovpn_/ {print $1}')
}

if [ "${OVPN_PROTO}" == "UDP" ]; then
	OVPN_NUM_PEERS=${OVPN_NUM_PEERS:-$(wc -l ${OVPN_UDP_PEERS_FILE} | \
		awk '{print $1}')}
else
	OVPN_NUM_PEERS=${OVPN_NUM_PEERS:-$(wc -l ${OVPN_TCP_PEERS_FILE} | \
		awk '{print $1}')}
fi
