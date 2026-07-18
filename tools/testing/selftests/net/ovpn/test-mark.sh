#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020-2025 OpenVPN, Inc.
#
#	Author:	Ralf Lici <ralf@mandelbit.com>
#		Antonio Quartulli <antonio@openvpn.net>

#set -x
set -eE

MARK=1056
MARK_DROP_COUNTER=0

source ./common.sh

ovpn_test_finished=0

ovpn_test_exit() {
	ovpn_cleanup
	modprobe -r ovpn || true

	if [ "${ovpn_test_finished}" -eq 0 ]; then
		ktap_print_totals
	fi
}

ovpn_mark_prepare_network() {
	local p
	local peer_ns

	for p in $(seq 0 "${OVPN_NUM_PEERS}"); do
		ovpn_cmd_ok "create namespace peer${p}" ovpn_create_ns "${p}"
	done

	for p in $(seq 0 3); do
		ovpn_cmd_ok "configure peer${p} namespace" ovpn_setup_ns \
			"${p}" 5.5.5.$((p + 1))/24
	done

	ovpn_cmd_ok "create server-side multi-peer with fwmark" \
		ip netns exec ovpn_peer0 "${OVPN_CLI}" new_multi_peer tun0 1 \
			ASYMM "${OVPN_UDP_PEERS_FILE}" "${MARK}"
	for p in $(seq 1 3); do
		ovpn_cmd_ok "install server key for peer ${p}" \
			ip netns exec ovpn_peer0 "${OVPN_CLI}" new_key tun0 \
				"${p}" 1 0 "${OVPN_ALG}" 0 data64.key
	done

	for p in $(seq 1 3); do
		ovpn_cmd_ok "register peer${p} in overlay" ovpn_add_peer "${p}"
	done

	for p in $(seq 1 3); do
		peer_ns="ovpn_peer${p}"
		ovpn_cmd_ok "set peer0 timeout for peer ${p}" \
			ip netns exec ovpn_peer0 "${OVPN_CLI}" set_peer tun0 \
				"${p}" 60 120
		ovpn_cmd_ok "set peer${p} timeout for peer ${p}" \
			ip netns exec "${peer_ns}" "${OVPN_CLI}" set_peer \
				tun"${p}" $((p + OVPN_ID_OFFSET)) 60 120
	done
}

ovpn_mark_run_baseline_traffic() {
	local p

	for p in $(seq 1 3); do
		ovpn_cmd_ok "send baseline traffic to peer ${p}" \
			ip netns exec ovpn_peer0 ping -qfc 100 -w 3 \
				5.5.5.$((p + 1))
	done
}

ovpn_mark_add_drop_rule() {
	ovpn_log "Adding an nftables drop rule based on mark value ${MARK}"

	ovpn_cmd_ok "flush nft ruleset" ip netns exec ovpn_peer0 nft flush \
		ruleset
	ovpn_cmd_ok "create nft filter table" ip netns exec ovpn_peer0 nft \
		"add table inet filter"
	ovpn_cmd_ok "create nft filter output chain" \
		ip netns exec ovpn_peer0 nft "add chain inet filter output { \
			type filter hook output priority 0; policy accept; }"
	ovpn_cmd_ok "add nft drop rule for mark ${MARK}" \
		ip netns exec ovpn_peer0 nft add rule inet filter output \
			meta mark == "${MARK}" \
			counter drop

	MARK_DROP_COUNTER=$(ip netns exec ovpn_peer0 nft list chain inet \
		filter output | sed -n 's/.*packets \([0-9]*\).*/\1/p')
	if [ -z "${MARK_DROP_COUNTER}" ]; then
		printf '%s\n' "unable to read nft drop counter"
		return 1
	fi
}

ovpn_mark_verify_drop_traffic() {
	local p
	local ping_output
	local lost_packets
	local total_count

	for p in $(seq 1 3); do
		if ping_output=$(ip netns exec ovpn_peer0 ping -qfc 100 -w 1 \
			5.5.5.$((p + 1)) 2>&1); then
			printf '%s\n' "expected ping to peer ${p} to fail \
				after nft drop rule"
			return 1
		fi
		ovpn_log "${ping_output}"
		lost_packets=$(echo "${ping_output}" | \
				awk '/packets transmitted/ { print $1 }')
		if [ -z "${lost_packets}" ]; then
			printf '%s\n' "unable to parse lost packets for peer \
				${p}"
			return 1
		fi
		MARK_DROP_COUNTER=$((MARK_DROP_COUNTER + lost_packets))
	done

	total_count=$(ip netns exec ovpn_peer0 nft list chain inet filter \
		output | sed -n 's/.*packets \([0-9]*\).*/\1/p')
	if [ -z "${total_count}" ]; then
		printf '%s\n' "unable to read final nft drop counter"
		return 1
	fi
	if [ "${MARK_DROP_COUNTER}" -ne "${total_count}" ]; then
		printf '%s\n' "expected ${MARK_DROP_COUNTER} drops, got \
			${total_count}"
		return 1
	fi
}

ovpn_mark_remove_drop_rule() {
	ovpn_log "Removing the drop rule"

	ovpn_cmd_ok "flush nft ruleset" ip netns exec ovpn_peer0 nft flush \
		ruleset
}

ovpn_mark_verify_traffic_recovery() {
	local p

	sleep 1
	for p in $(seq 1 3); do
		ovpn_cmd_ok "send recovery traffic to peer ${p}" \
			ip netns exec ovpn_peer0 ping -qfc 100 -w 3 \
				5.5.5.$((p + 1))
	done
}

trap ovpn_test_exit EXIT
trap ovpn_stage_err ERR

ktap_print_header
ktap_set_plan 6

ovpn_cleanup
modprobe -q ovpn || true

ovpn_run_stage "setup marked network topology" ovpn_mark_prepare_network
ovpn_run_stage "run baseline traffic" ovpn_mark_run_baseline_traffic
ovpn_run_stage "install nft mark drop rule" ovpn_mark_add_drop_rule
ovpn_run_stage "drop marked traffic and count packets" \
	ovpn_mark_verify_drop_traffic
ovpn_run_stage "remove nft drop rule" ovpn_mark_remove_drop_rule
ovpn_run_stage "traffic recovers after drop removal" \
	ovpn_mark_verify_traffic_recovery

ovpn_test_finished=1
ktap_finished
