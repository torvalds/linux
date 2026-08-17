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
		ovpn_cmd_ok "start notification listener peer${p}" \
			ovpn_setup_listener "${p}"
		# starting all YNL listeners back-to-back can intermittently
		# stall their startup so serialize launches a bit
		sleep 0.5
	done

	for p in $(seq 0 ${OVPN_NUM_PEERS}); do
		ovpn_cmd_ok "configure peer${p} namespace" ovpn_setup_ns \
			"${p}" 5.5.5.$((p + 1))/24 "${MTU}"
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

ovpn_run_basic_traffic() {
	local p
	local header1
	local header2
	local peer_ns
	local raddr
	local tcpdump_pid1
	local tcpdump_pid2
	local tcpdump_timeout="1.5s"

	for p in $(seq 1 ${OVPN_NUM_PEERS}); do
		# The first part of the data packet header consists of:
		# - TCP only: 2 bytes for the packet length
		# - 5 bits for opcode ("9" for DATA_V2)
		# - 3 bits for key-id ("0" at this point)
		# - 12 bytes for peer-id:
		#     - with asymmetric ID: "${p}" one way and "${p} + 9" the
		#	other way
		#     - with symmetric ID: "${p}" both ways
		header1=$(printf "0x4800000%x" ${p})
		header2=$(printf "0x4800000%x" $((p + OVPN_ID_OFFSET)))
		raddr=""
		if [ "${OVPN_PROTO}" == "UDP" ]; then
			raddr=$(awk "NR == ${p} {print \$3}" \
				"${OVPN_UDP_PEERS_FILE}")
		fi
		peer_ns="ovpn_peer${p}"

		timeout ${tcpdump_timeout} ip netns exec "${peer_ns}" \
			tcpdump --immediate-mode -p -ni veth${p} -c 1 \
			"$(ovpn_build_capture_filter "${header1}" "${raddr}")" \
			>/dev/null 2>&1 &
		tcpdump_pid1=$!
		timeout ${tcpdump_timeout} ip netns exec "${peer_ns}" \
			tcpdump --immediate-mode -p -ni veth${p} -c 1 \
			"$(ovpn_build_capture_filter "${header2}" "${raddr}")" \
			>/dev/null 2>&1 &
		tcpdump_pid2=$!

		sleep 0.3
		ovpn_cmd_ok "send baseline traffic to peer ${p}" \
			ip netns exec ovpn_peer0 \
			ping -qfc 100 -w 3 5.5.5.$((p + 1))
		ovpn_cmd_ok "send large-payload traffic to peer ${p}" \
			ip netns exec ovpn_peer0 \
			ping -qfc 100 -s 3000 -w 3 5.5.5.$((p + 1))

		wait "${tcpdump_pid1}" || return 1
		wait "${tcpdump_pid2}" || return 1
	done
}

ovpn_run_lan_traffic() {
	ovpn_cmd_ok "ping LAN behind peer1" \
		ip netns exec ovpn_peer0 ping -qfc 100 -w 3 "${OVPN_LAN_IP}"
}

ovpn_run_float_mode() {
	local p
	local peer_ns

	for p in $(seq 1 ${OVPN_NUM_PEERS}); do
		peer_ns="ovpn_peer${p}"
		ovpn_cmd_ok "float: remove old transport address on peer${p}" \
			ip -n "${peer_ns}" addr del 10.10.${p}.2/24 dev veth${p}
		ovpn_cmd_ok "float: add new transport address on peer${p}" \
			ip -n "${peer_ns}" addr add 10.10.${p}.3/24 dev veth${p}
	done
	for p in $(seq 1 ${OVPN_NUM_PEERS}); do
		peer_ns="ovpn_peer${p}"
		ovpn_cmd_ok "ping tunnel after float peer ${p}" \
			ip netns exec "${peer_ns}" ping -qfc 100 -w 3 5.5.5.1
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

ovpn_run_key_rollover() {
	local p
	local peer_ns

	ovpn_log "Adding secondary key and then swap:"

	for p in $(seq 1 ${OVPN_NUM_PEERS}); do
		peer_ns="ovpn_peer${p}"
		ovpn_cmd_ok "add secondary key on peer0 for peer ${p}" \
			ip netns exec ovpn_peer0 ${OVPN_CLI} new_key tun0 \
				${p} 2 1 ${OVPN_ALG} 0 data64.key
		ovpn_cmd_ok "add secondary key on peer${p} for peer ${p}" \
			ip netns exec "${peer_ns}" ${OVPN_CLI} new_key tun${p} \
				$((p + OVPN_ID_OFFSET)) 2 1 ${OVPN_ALG} 1 \
				data64.key
		ovpn_cmd_ok "swap keys on peer${p}" \
			ip netns exec "${peer_ns}" ${OVPN_CLI} swap_keys \
				tun${p} $((p + OVPN_ID_OFFSET))
	done
}

ovpn_run_queries() {
	ovpn_log "Querying all peers:"

	ovpn_cmd_ok "query all peers from peer0" \
		ip netns exec ovpn_peer0 ${OVPN_CLI} get_peer tun0
	ovpn_cmd_ok "query all peers from peer1" \
		ip netns exec ovpn_peer1 ${OVPN_CLI} get_peer tun1

	ovpn_log "Querying peer 1:"

	ovpn_cmd_ok "query peer 1 from peer0" \
		ip netns exec ovpn_peer0 ${OVPN_CLI} get_peer tun0 1
}

ovpn_query_peer_missing() {
	ovpn_log "Querying non-existent peer 20:"

	ovpn_cmd_fail "query missing peer 20 on peer0" \
		ip netns exec ovpn_peer0 ${OVPN_CLI} get_peer tun0 20
}

ovpn_run_peer_cleanup() {
	local p
	local peer_ns

	ovpn_log "Deleting peer 1:"

	ovpn_cmd_ok "delete peer1 on peer0" \
		ip netns exec ovpn_peer0 ${OVPN_CLI} del_peer tun0 1
	ovpn_cmd_ok "delete peer1 on peer1" \
		ip netns exec ovpn_peer1 ${OVPN_CLI} del_peer tun1 \
			$((1 + OVPN_ID_OFFSET))

	ovpn_log "Querying keys:"

	for p in $(seq 2 ${OVPN_NUM_PEERS}); do
		peer_ns="ovpn_peer${p}"
		ovpn_cmd_ok "query peer${p} key 1" \
			ip netns exec "${peer_ns}" ${OVPN_CLI} get_key tun${p} \
				$((p + OVPN_ID_OFFSET)) 1
		ovpn_cmd_ok "query peer${p} key 2" \
			ip netns exec "${peer_ns}" ${OVPN_CLI} get_key tun${p} \
				$((p + OVPN_ID_OFFSET)) 2
	done
}

ovpn_run_traffic_delete_peer() {
	local ping_pid

	ovpn_log "Deleting peer while sending traffic:"

	ovpn_run_bg ping_pid ip netns exec ovpn_peer2 ping -qf -w 4 5.5.5.1
	sleep 2
	ovpn_cmd_ok "delete peer0 peer 2" \
		ip netns exec ovpn_peer0 ${OVPN_CLI} del_peer tun0 2

	if [ "${OVPN_PROTO}" == "TCP" ]; then
		# In TCP mode this command is expected to fail for both peers.
		ovpn_cmd_mayfail "delete peer2 peer 2 (TCP non-fatal)" \
			ip netns exec ovpn_peer2 ${OVPN_CLI} del_peer tun2 \
				$((2 + OVPN_ID_OFFSET))
	else
		ovpn_cmd_ok "delete peer2 peer 2" ip netns exec ovpn_peer2 \
			${OVPN_CLI} del_peer tun2 $((2 + OVPN_ID_OFFSET))
	fi

	wait "${ping_pid}" || true
}

ovpn_run_key_cleanup() {
	local p
	local peer_ns

	ovpn_log "Deleting keys:"

	for p in $(seq 3 ${OVPN_NUM_PEERS}); do
		peer_ns="ovpn_peer${p}"
		ovpn_cmd_ok "delete key 1 for peer${p}" \
			ip netns exec "${peer_ns}" ${OVPN_CLI} del_key tun${p} \
				$((p + OVPN_ID_OFFSET)) 1
		ovpn_cmd_ok "delete key 2 for peer${p}" \
			ip netns exec "${peer_ns}" ${OVPN_CLI} del_key tun${p} \
				$((p + OVPN_ID_OFFSET)) 2
	done
}

ovpn_run_timeouts() {
	local p
	local peer_ns

	ovpn_log "Setting timeout to 3s MP:"

	for p in $(seq 3 ${OVPN_NUM_PEERS}); do
		# Non-fatal: this may fail in some protocol modes.
		ovpn_cmd_mayfail "set peer0 timeout for peer ${p} (non-fatal)" \
			ip netns exec ovpn_peer0 ${OVPN_CLI} set_peer tun0 \
				${p} 3 3
		peer_ns="ovpn_peer${p}"
		ovpn_cmd_ok "disable timeout on peer${p} while peer0 adjusts \
			state" ip netns exec "${peer_ns}" ${OVPN_CLI} set_peer \
			tun${p} $((p + OVPN_ID_OFFSET)) 0 0
	done
	# wait for peers to timeout
	sleep 5

	ovpn_log "Setting timeout to 3s P2P:"

	for p in $(seq 3 ${OVPN_NUM_PEERS}); do
		peer_ns="ovpn_peer${p}"
		ovpn_cmd_ok "set peer${p} P2P timeout" \
			ip netns exec "${peer_ns}" ${OVPN_CLI} set_peer \
				tun${p} $((p + OVPN_ID_OFFSET)) 3 3
	done
	sleep 5
}

ovpn_run_notifications() {
	local p

	for p in $(seq 0 ${OVPN_NUM_PEERS}); do
		ovpn_cmd_ok "validate listener output for peer ${p}" \
			ovpn_compare_ntfs "${p}"
	done
}

trap ovpn_test_exit EXIT
trap ovpn_stage_err ERR

ktap_print_header
if [ "${OVPN_FLOAT}" == "1" ]; then
	ktap_set_plan 13
else
	ktap_set_plan 12
fi

ovpn_cleanup
modprobe -q ovpn || true

ovpn_run_stage "setup network topology" ovpn_prepare_network
ovpn_run_stage "run baseline data traffic" ovpn_run_basic_traffic
ovpn_run_stage "run LAN traffic behind peer1" ovpn_run_lan_traffic
[ "${OVPN_FLOAT}" == "1" ] && ovpn_run_stage "run floating peer checks" \
	ovpn_run_float_mode
ovpn_run_stage "run iperf throughput" ovpn_run_iperf
ovpn_run_stage "run key rollout" ovpn_run_key_rollover
ovpn_run_stage "query peers" ovpn_run_queries
ovpn_run_stage "query missing peer fails" ovpn_query_peer_missing
ovpn_run_stage "peer lifecycle and key queries" ovpn_run_peer_cleanup
ovpn_run_stage "delete peer while traffic" ovpn_run_traffic_delete_peer
ovpn_run_stage "delete stale keys" ovpn_run_key_cleanup
ovpn_run_stage "check timeout behavior" ovpn_run_timeouts
ovpn_run_stage "validate notification output" ovpn_run_notifications

ovpn_test_finished=1
ktap_finished
