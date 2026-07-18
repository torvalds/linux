#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020-2025 OpenVPN, Inc.
#
#  Author:	Antonio Quartulli <antonio@openvpn.net>

#set -x
set -eE

source ./common.sh

ovpn_test_finished=0

ovpn_test_exit() {
	ovpn_cleanup
	modprobe -r ovpn || true

	if [ "${ovpn_test_finished}" -eq 0 ]; then
		ktap_print_totals
	fi
}

ovpn_prepare_network() {
	local p
	local peer_ns

	for p in $(seq 0 ${OVPN_NUM_PEERS}); do
		ovpn_cmd_ok "create namespace peer${p}" ovpn_create_ns "${p}"
	done

	for p in $(seq 0 ${OVPN_NUM_PEERS}); do
		ovpn_cmd_ok "configure peer${p} namespace" ovpn_setup_ns \
			"${p}" 5.5.5.$((p + 1))/24
	done

	for p in $(seq 0 ${OVPN_NUM_PEERS}); do
		ovpn_cmd_ok "register peer${p} in overlay" ovpn_add_peer "${p}"
	done

	for p in $(seq 1 ${OVPN_NUM_PEERS}); do
		peer_ns="ovpn_peer${p}"
		ovpn_cmd_ok "set peer0 timeout for peer ${p}" \
			ip netns exec ovpn_peer0 ${OVPN_CLI} set_peer tun0 \
				${p} 60 120
		ovpn_cmd_ok "set peer${p} timeout for peer ${p}" \
			ip netns exec "${peer_ns}" ${OVPN_CLI} set_peer \
				tun${p} $((p + OVPN_ID_OFFSET)) 60 120
	done
}

ovpn_run_ping_traffic() {
	local p

	for p in $(seq 1 ${OVPN_NUM_PEERS}); do
		ovpn_cmd_ok "send ping traffic to peer ${p}" \
			ip netns exec ovpn_peer0 ping -qfc 100 -w 3 \
				5.5.5.$((p + 1))
	done
}

ovpn_run_iperf() {
	local iperf_pid

	ovpn_run_bg iperf_pid ip netns exec ovpn_peer0 iperf3 -1 -s
	sleep 1
	ovpn_cmd_ok "run iperf throughput flow" \
		ip netns exec ovpn_peer1 iperf3 -Z -t 3 -c 5.5.5.1
	wait "${iperf_pid}" || return 1
}

trap ovpn_test_exit EXIT
trap ovpn_stage_err ERR

ktap_print_header
ktap_set_plan 3

ovpn_cleanup
modprobe -q ovpn || true

ovpn_run_stage "setup network topology" ovpn_prepare_network
ovpn_run_stage "run ping traffic" ovpn_run_ping_traffic
ovpn_run_stage "run iperf throughput" ovpn_run_iperf

ovpn_test_finished=1
ktap_finished
