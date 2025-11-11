#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# This selftest exercises trying to have multiple netpoll users at the same
# time.
#
# This selftest has multiple smalls test inside, and the goal is to
# get interfaces with bonding and netconsole in different orders in order
# to catch any possible issue.
#
# The main test composes of four interfaces being created using netdevsim; two
# of them are bonded to serve as the netconsole's transmit interface. The
# remaining two interfaces are similarly bonded and assigned to a separate
# network namespace, which acts as the receive interface, where socat monitors
# for incoming messages.
#
# A netconsole message is then sent to ensure it is properly received across
# this configuration.
#
# Later, run a few other tests, to make sure that bonding and netconsole
# cannot coexist.
#
# The test's objective is to exercise netpoll usage when managed simultaneously
# by multiple subsystems (netconsole and bonding).
#
# Author: Breno Leitao <leitao@debian.org>

set -euo pipefail

SCRIPTDIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

source "${SCRIPTDIR}"/../lib/sh/lib_netcons.sh

modprobe netdevsim 2> /dev/null || true
modprobe netconsole 2> /dev/null || true
modprobe bonding 2> /dev/null || true
modprobe veth 2> /dev/null || true

# The content of kmsg will be save to the following file
OUTPUT_FILE="/tmp/${TARGET}"

# Check for basic system dependency and exit if not found
check_for_dependencies
# Set current loglevel to KERN_INFO(6), and default to KERN_NOTICE(5)
echo "6 5" > /proc/sys/kernel/printk
# Remove the namespace, interfaces and netconsole target on exit
trap cleanup_bond EXIT

FORMAT="extended"
IP_VERSION="ipv4"
VETH0="veth"$(( RANDOM % 256))
VETH1="veth"$((256 +  RANDOM % 256))
TXNS=""
RXNS=""

# Create "bond_tx_XX" and "bond_rx_XX" interfaces, and set DSTIF and SRCIF with
# the bonding interfaces
function setup_bonding_ifaces() {
	local RAND=$(( RANDOM % 100 ))
	BOND_TX_MAIN_IF="bond_tx_$RAND"
	BOND_RX_MAIN_IF="bond_rx_$RAND"

	# Setup TX
	if ! ip -n "${TXNS}" link add "${BOND_TX_MAIN_IF}" type bond mode balance-rr
	then
		echo "Failed to create bond TX interface. Is CONFIG_BONDING set?" >&2
		# only clean nsim ifaces and namespace. Nothing else has been
		# initialized
		cleanup_bond_nsim
		trap - EXIT
		exit "${ksft_skip}"
	fi

	# create_netdevsim() got the interface up, but it needs to be down
	# before being enslaved.
	ip -n "${TXNS}" \
		link set "${BOND_TX1_SLAVE_IF}" down
	ip -n "${TXNS}" \
		link set "${BOND_TX2_SLAVE_IF}" down
	ip -n "${TXNS}" \
		link set "${BOND_TX1_SLAVE_IF}" master "${BOND_TX_MAIN_IF}"
	ip -n "${TXNS}" \
		link set "${BOND_TX2_SLAVE_IF}" master "${BOND_TX_MAIN_IF}"
	ip -n "${TXNS}" \
		link set "${BOND_TX_MAIN_IF}" up

	# Setup RX
	ip -n "${RXNS}" \
		link add "${BOND_RX_MAIN_IF}" type bond mode balance-rr
	ip -n "${RXNS}" \
		link set "${BOND_RX1_SLAVE_IF}" down
	ip -n "${RXNS}" \
		link set "${BOND_RX2_SLAVE_IF}" down
	ip -n "${RXNS}" \
		link set "${BOND_RX1_SLAVE_IF}" master "${BOND_RX_MAIN_IF}"
	ip -n "${RXNS}" \
		link set "${BOND_RX2_SLAVE_IF}" master "${BOND_RX_MAIN_IF}"
	ip -n "${RXNS}" \
		link set "${BOND_RX_MAIN_IF}" up

	export DSTIF="${BOND_RX_MAIN_IF}"
	export SRCIF="${BOND_TX_MAIN_IF}"
}

# Create 4 netdevsim interfaces. Two of them will be bound to TX bonding iface
# and the other two will be bond to the RX interface (on the other namespace)
function create_ifaces_bond() {
	BOND_TX1_SLAVE_IF=$(create_netdevsim "${NSIM_BOND_TX_1}" "${TXNS}")
	BOND_TX2_SLAVE_IF=$(create_netdevsim "${NSIM_BOND_TX_2}" "${TXNS}")
	BOND_RX1_SLAVE_IF=$(create_netdevsim "${NSIM_BOND_RX_1}" "${RXNS}")
	BOND_RX2_SLAVE_IF=$(create_netdevsim "${NSIM_BOND_RX_2}" "${RXNS}")
}

# netdevsim link BOND_TX to BOND_RX interfaces
function link_ifaces_bond() {
	local BOND_TX1_SLAVE_IFIDX
	local BOND_TX2_SLAVE_IFIDX
	local BOND_RX1_SLAVE_IFIDX
	local BOND_RX2_SLAVE_IFIDX
	local TXNS_FD
	local RXNS_FD

	BOND_TX1_SLAVE_IFIDX=$(ip netns exec "${TXNS}" \
				cat /sys/class/net/"$BOND_TX1_SLAVE_IF"/ifindex)
	BOND_TX2_SLAVE_IFIDX=$(ip netns exec "${TXNS}" \
				cat /sys/class/net/"$BOND_TX2_SLAVE_IF"/ifindex)
	BOND_RX1_SLAVE_IFIDX=$(ip netns exec "${RXNS}" \
				cat /sys/class/net/"$BOND_RX1_SLAVE_IF"/ifindex)
	BOND_RX2_SLAVE_IFIDX=$(ip netns exec "${RXNS}" \
				cat /sys/class/net/"$BOND_RX2_SLAVE_IF"/ifindex)

	exec {TXNS_FD}</var/run/netns/"${TXNS}"
	exec {RXNS_FD}</var/run/netns/"${RXNS}"

	# Linking TX ifaces to the RX ones (on the other namespace)
	echo "${TXNS_FD}:$BOND_TX1_SLAVE_IFIDX $RXNS_FD:$BOND_RX1_SLAVE_IFIDX"  \
		> "$NSIM_DEV_SYS_LINK"
	echo "${TXNS_FD}:$BOND_TX2_SLAVE_IFIDX $RXNS_FD:$BOND_RX2_SLAVE_IFIDX"  \
		> "$NSIM_DEV_SYS_LINK"

	exec {TXNS_FD}<&-
	exec {RXNS_FD}<&-
}

function create_all_ifaces() {
	# setup_ns function is coming from lib.sh
	setup_ns TXNS RXNS
	export NAMESPACE="${RXNS}"

	# Create two interfaces for RX and two for TX
	create_ifaces_bond
	# Link netlink ifaces
	link_ifaces_bond
}

# configure DSTIF and SRCIF IPs
function configure_ifaces_ips() {
	local IP_VERSION=${1:-"ipv4"}
	select_ipv4_or_ipv6 "${IP_VERSION}"

	ip -n "${RXNS}" addr add "${DSTIP}"/24 dev "${DSTIF}"
	ip -n "${RXNS}" link set "${DSTIF}" up

	ip -n "${TXNS}" addr add "${SRCIP}"/24 dev "${SRCIF}"
	ip -n "${TXNS}" link set "${SRCIF}" up
}

function test_enable_netpoll_on_enslaved_iface() {
	echo 0 > "${NETCONS_PATH}"/enabled

	# At this stage, BOND_TX1_SLAVE_IF is enslaved to BOND_TX_MAIN_IF, and
	# linked to BOND_RX1_SLAVE_IF inside the namespace.
	echo "${BOND_TX1_SLAVE_IF}" > "${NETCONS_PATH}"/dev_name

	# This should fail with the following message in dmesg:
	# netpoll: netconsole: ethX is a slave device, aborting
	set +e
	enable_netcons_ns 2> /dev/null
	set -e

	if [[ $(cat "${NETCONS_PATH}"/enabled) -eq 1 ]]
	then
		echo "test failed: Bonding and netpoll cannot co-exists." >&2
		exit "${ksft_fail}"
	fi
}

function test_delete_bond_and_reenable_target() {
	ip -n "${TXNS}" \
		link delete "${BOND_TX_MAIN_IF}" type bond

	# BOND_TX1_SLAVE_IF is not attached to a bond interface anymore
	# netpoll can be plugged in there
	echo "${BOND_TX1_SLAVE_IF}" > "${NETCONS_PATH}"/dev_name

	# this should work, since the interface is not enslaved
	enable_netcons_ns

	if [[ $(cat "${NETCONS_PATH}"/enabled) -eq 0 ]]
	then
		echo "test failed: Unable to start netpoll on an unbond iface." >&2
		exit "${ksft_fail}"
	fi
}

# Send a netconsole message to the netconsole target
function test_send_netcons_msg_through_bond_iface() {
	# Listen for netconsole port inside the namespace and
	# destination interface
	listen_port_and_save_to "${OUTPUT_FILE}" "${IP_VERSION}" &
	# Wait for socat to start and listen to the port.
	wait_for_port "${RXNS}" "${PORT}" "${IP_VERSION}"
	# Send the message
	echo "${MSG}: ${TARGET}" > /dev/kmsg
	# Wait until socat saves the file to disk
	busywait "${BUSYWAIT_TIMEOUT}" test -s "${OUTPUT_FILE}"
	# Make sure the message was received in the dst part
	# and exit
	validate_result "${OUTPUT_FILE}" "${FORMAT}"
	# kill socat in case it is still running
	pkill_socat
}

# BOND_TX1_SLAVE_IF has netconsole enabled on it, bind it to BOND_TX_MAIN_IF.
# Given BOND_TX_MAIN_IF was deleted, recreate it first
function test_enslave_netcons_enabled_iface {
	# netconsole got disabled while the interface was down
	if [[ $(cat "${NETCONS_PATH}"/enabled) -eq 0 ]]
	then
		echo "test failed: netconsole expected to be enabled against BOND_TX1_SLAVE_IF" >&2
		exit "${ksft_fail}"
	fi

	# recreate the bonding iface. it got deleted by previous
	# test (test_delete_bond_and_reenable_target)
	ip -n "${TXNS}" \
		link add "${BOND_TX_MAIN_IF}" type bond mode balance-rr

	# sub-interface need to be down before attaching to bonding
	# This will also disable netconsole.
	ip -n "${TXNS}" \
		link set "${BOND_TX1_SLAVE_IF}" down
	ip -n "${TXNS}" \
		link set "${BOND_TX1_SLAVE_IF}" master "${BOND_TX_MAIN_IF}"
	ip -n "${TXNS}" \
		link set "${BOND_TX_MAIN_IF}" up

	# netconsole got disabled while the interface was down
	if [[ $(cat "${NETCONS_PATH}"/enabled) -eq 1 ]]
	then
		echo "test failed: Device is part of a bond iface, cannot have netcons enabled" >&2
		exit "${ksft_fail}"
	fi
}

# Get netconsole enabled on a bonding interface and attach a second
# sub-interface.
function test_enslave_iface_to_bond {
	# BOND_TX_MAIN_IF has only BOND_TX1_SLAVE_IF right now
	echo "${BOND_TX_MAIN_IF}" > "${NETCONS_PATH}"/dev_name
	enable_netcons_ns

	# netcons is attached to bond0 and BOND_TX1_SLAVE_IF is
	# part of BOND_TX_MAIN_IF. Attach BOND_TX2_SLAVE_IF to BOND_TX_MAIN_IF.
	ip -n "${TXNS}" \
		link set "${BOND_TX2_SLAVE_IF}" master "${BOND_TX_MAIN_IF}"
	if [[ $(cat "${NETCONS_PATH}"/enabled) -eq 0 ]]
	then
		echo "test failed: Netconsole should be enabled on bonding interface. Failed" >&2
		exit "${ksft_fail}"
	fi
}

function test_enslave_iff_disabled_netpoll_iface {
	local ret

	# Create two interfaces. veth interfaces it known to have
	# IFF_DISABLE_NETPOLL set
	if ! ip link add "${VETH0}" type veth peer name "${VETH1}"
	then
		echo "Failed to create veth TX interface. Is CONFIG_VETH set?" >&2
		exit "${ksft_skip}"
	fi
	set +e
	# This will print RTNETLINK answers: Device or resource busy
	ip link set "${VETH0}" master "${BOND_TX_MAIN_IF}" 2> /dev/null
	ret=$?
	set -e
	if [[ $ret -eq 0 ]]
	then
		echo "test failed: veth interface could not be enslaved"
		exit "${ksft_fail}"
	fi
}

# Given that netconsole picks the current net namespace, we need to enable it
# from inside the TXNS namespace
function enable_netcons_ns() {
	ip netns exec "${TXNS}" sh -c \
		"mount -t configfs configfs /sys/kernel/config && echo 1 > $NETCONS_PATH/enabled"
}

####################
# Tests start here #
####################

# Create regular interfaces using netdevsim and link them
create_all_ifaces

# Setup the bonding interfaces
# BOND_RX_MAIN_IF has BOND_RX{1,2}_SLAVE_IF
# BOND_TX_MAIN_IF has BOND_TX{1,2}_SLAVE_IF
setup_bonding_ifaces

# Configure the ips as BOND_RX1_SLAVE_IF and BOND_TX1_SLAVE_IF
configure_ifaces_ips "${IP_VERSION}"

_create_dynamic_target "${FORMAT}" "${NETCONS_PATH}"
enable_netcons_ns
set_user_data

# Test #1 : Create an bonding interface and attach netpoll into
# the bonding interface. Netconsole/netpoll should work on
# the bonding interface.
test_send_netcons_msg_through_bond_iface
echo "test #1: netpoll on bonding interface worked. Test passed" >&2

# Test #2: Attach netpoll to an enslaved interface
# Try to attach netpoll to an enslaved sub-interface (while still being part of
# a bonding interface), which shouldn't be allowed
test_enable_netpoll_on_enslaved_iface
echo "test #2: netpoll correctly rejected enslaved interface (expected behavior). Test passed." >&2

# Test #3: Unplug the sub-interface from bond and enable netconsole
# Detach the interface from a bonding interface and attach netpoll again
test_delete_bond_and_reenable_target
echo "test #3: Able to attach to an unbound interface. Test passed." >&2

# Test #4: Enslave a sub-interface that had netconsole enabled
# Try to enslave an interface that has netconsole/netpoll enabled.
# Previous test has netconsole enabled in BOND_TX1_SLAVE_IF, try to enslave it
test_enslave_netcons_enabled_iface
echo "test #4: Enslaving an interface with netpoll attached. Test passed." >&2

# Test #5: Enslave a sub-interface to a bonding interface
# Enslave an interface to a bond interface that has netpoll attached
# At this stage, BOND_TX_MAIN_IF is created and BOND_TX1_SLAVE_IF is part of
# it. Netconsole is currently disabled
test_enslave_iface_to_bond
echo "test #5: Enslaving an interface to bond+netpoll. Test passed." >&2

# Test #6: Enslave a IFF_DISABLE_NETPOLL sub-interface to a bonding interface
# At this stage, BOND_TX_MAIN_IF has both sub interface and netconsole is
# enabled. This test will try to enslave an a veth (IFF_DISABLE_NETPOLL) interface
# and it should fail, with netpoll: veth0 doesn't support polling
test_enslave_iff_disabled_netpoll_iface
echo "test #6: Enslaving IFF_DISABLE_NETPOLL ifaces to bond iface is not supported. Test passed." >&2

cleanup_bond
trap - EXIT
exit "${EXIT_STATUS}"
