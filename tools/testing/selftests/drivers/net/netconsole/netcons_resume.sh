#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

# This test validates that netconsole is able to resume a target that was
# deactivated when its interface was removed when the interface is brought
# back up.
#
# The test configures a netconsole target and then removes netdevsim module to
# cause the interface to disappear. Targets are configured via cmdline to ensure
# targets bound by interface name and mac address can be resumed.
# The test verifies that the target moved to disabled state before adding
# netdevsim and the interface back.
#
# Finally, the test verifies that the target is re-enabled automatically and
# the message is received on the destination interface.
#
# Author: Andre Carvalho <asantostc@gmail.com>

set -euo pipefail

SCRIPTDIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

source "${SCRIPTDIR}"/../lib/sh/lib_netcons.sh

SAVED_SRCMAC="" # to be populated later
SAVED_DSTMAC="" # to be populated later

modprobe netdevsim 2> /dev/null || true
rmmod netconsole 2> /dev/null || true

check_netconsole_module

function cleanup() {
	cleanup_netcons "${NETCONS_CONFIGFS}/cmdline0"
	do_cleanup
	rmmod netconsole
}

function trigger_reactivation() {
	# Add back low level module
	modprobe netdevsim
	# Recreate namespace and two interfaces
	set_network
	# Restore MACs
	ip netns exec "${NAMESPACE}" ip link set "${DSTIF}" \
		address "${SAVED_DSTMAC}"
	if [ "${BINDMODE}" == "mac" ]; then
		ip link set dev "${SRCIF}" down
		ip link set dev "${SRCIF}" address "${SAVED_SRCMAC}"
		# Rename device in order to trigger target resume, as initial
		# when device was recreated it didn't have correct mac address.
		ip link set dev "${SRCIF}" name "${TARGET}"
	fi
}

function trigger_deactivation() {
	# Start by storing mac addresses so we can be restored in reactivate
	SAVED_DSTMAC=$(ip netns exec "${NAMESPACE}" \
		cat /sys/class/net/"$DSTIF"/address)
	SAVED_SRCMAC=$(mac_get "${SRCIF}")
	# Remove low level module
	rmmod netdevsim
}

trap cleanup EXIT

# Run the test twice, with different cmdline parameters
for BINDMODE in "ifname" "mac"
do
	echo "Running with bind mode: ${BINDMODE}" >&2
	# Set current loglevel to KERN_INFO(6), and default to KERN_NOTICE(5)
	echo "6 5" > /proc/sys/kernel/printk

	# Create one namespace and two interfaces
	set_network

	# Create the command line for netconsole, with the configuration from
	# the function above
	CMDLINE=$(create_cmdline_str "${BINDMODE}")

	# The content of kmsg will be save to the following file
	OUTPUT_FILE="/tmp/${TARGET}-${BINDMODE}"

	# Load the module, with the cmdline set
	modprobe netconsole "${CMDLINE}"
	# Expose cmdline target in configfs
	mkdir "${NETCONS_CONFIGFS}/cmdline0"

	# Target should be enabled
	wait_target_state "cmdline0" "enabled"

	# Trigger deactivation by unloading netdevsim module. Target should be
	# disabled.
	trigger_deactivation
	wait_target_state "cmdline0" "disabled"

	# Trigger reactivation by loading netdevsim, recreating the network and
	# restoring mac addresses. Target should be re-enabled.
	trigger_reactivation
	wait_target_state "cmdline0" "enabled"

	# Listen for netconsole port inside the namespace and destination
	# interface
	listen_port_and_save_to "${OUTPUT_FILE}" &
	# Wait for socat to start and listen to the port.
	wait_local_port_listen "${NAMESPACE}" "${PORT}" udp
	# Send the message
	echo "${MSG}: ${TARGET}" > /dev/kmsg
	# Wait until socat saves the file to disk
	busywait "${BUSYWAIT_TIMEOUT}" test -s "${OUTPUT_FILE}"
	# Make sure the message was received in the dst part
	# and exit
	validate_msg "${OUTPUT_FILE}"

	# kill socat in case it is still running
	pkill_socat
	# Cleanup & unload the module
	cleanup

	echo "${BINDMODE} : Test passed" >&2
done

trap - EXIT
exit "${EXIT_STATUS}"
