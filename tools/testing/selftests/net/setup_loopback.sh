#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

readonly FLUSH_PATH="/sys/class/net/${dev}/gro_flush_timeout"
readonly IRQ_PATH="/sys/class/net/${dev}/napi_defer_hard_irqs"
readonly FLUSH_TIMEOUT="$(< ${FLUSH_PATH})"
readonly HARD_IRQS="$(< ${IRQ_PATH})"

netdev_check_for_carrier() {
	local -r dev="$1"

	for i in {1..5}; do
		carrier="$(cat /sys/class/net/${dev}/carrier)"
		if [[ "${carrier}" -ne 1 ]] ; then
			echo "carrier not ready yet..." >&2
			sleep 1
		else
			echo "carrier ready" >&2
			break
		fi
	done
	echo "${carrier}"
}

# Assumes that there is no existing ipvlan device on the physical device
setup_loopback_environment() {
	local dev="$1"

	# Fail hard if cannot turn on loopback mode for current NIC
	ethtool -K "${dev}" loopback on || exit 1
	sleep 1

	# Check for the carrier
	carrier=$(netdev_check_for_carrier ${dev})
	if [[ "${carrier}" -ne 1 ]] ; then
		echo "setup_loopback_environment failed"
		exit 1
	fi
}

setup_macvlan_ns(){
	local -r link_dev="$1"
	local -r ns_name="$2"
	local -r ns_dev="$3"
	local -r ns_mac="$4"
	local -r addr="$5"

	ip link add link "${link_dev}" dev "${ns_dev}" \
		address "${ns_mac}" type macvlan
	exit_code=$?
	if [[ "${exit_code}" -ne 0 ]]; then
		echo "setup_macvlan_ns failed"
		exit $exit_code
	fi

	[[ -e /var/run/netns/"${ns_name}" ]] || ip netns add "${ns_name}"
	ip link set dev "${ns_dev}" netns "${ns_name}"
	ip -netns "${ns_name}" link set dev "${ns_dev}" up
	if [[ -n "${addr}" ]]; then
		ip -netns "${ns_name}" addr add dev "${ns_dev}" "${addr}"
	fi

	sleep 1
}

cleanup_macvlan_ns(){
	while (( $# >= 2 )); do
		ns_name="$1"
		ns_dev="$2"
		ip -netns "${ns_name}" link del dev "${ns_dev}"
		ip netns del "${ns_name}"
		shift 2
	done
}

cleanup_loopback(){
	local -r dev="$1"

	ethtool -K "${dev}" loopback off
	sleep 1

	# Check for the carrier
	carrier=$(netdev_check_for_carrier ${dev})
	if [[ "${carrier}" -ne 1 ]] ; then
		echo "setup_loopback_environment failed"
		exit 1
	fi
}

setup_interrupt() {
	# Use timer on  host to trigger the network stack
	# Also disable device interrupt to not depend on NIC interrupt
	# Reduce test flakiness caused by unexpected interrupts
	echo 100000 >"${FLUSH_PATH}"
	echo 50 >"${IRQ_PATH}"
}

setup_ns() {
	# Set up server_ns namespace and client_ns namespace
	setup_macvlan_ns "${dev}" server_ns server "${SERVER_MAC}"
	setup_macvlan_ns "${dev}" client_ns client "${CLIENT_MAC}"
}

cleanup_ns() {
	cleanup_macvlan_ns server_ns server client_ns client
}

setup() {
	setup_loopback_environment "${dev}"
	setup_interrupt
}

cleanup() {
	cleanup_loopback "${dev}"

	echo "${FLUSH_TIMEOUT}" >"${FLUSH_PATH}"
	echo "${HARD_IRQS}" >"${IRQ_PATH}"
}
