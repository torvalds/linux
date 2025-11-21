#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

# This is a selftest to test cmdline arguments on netconsole.
# It exercises loading of netconsole from cmdline instead of the dynamic
# reconfiguration. This includes parsing the long netconsole= line and all the
# flow through init_netconsole().
#
# Author: Breno Leitao <leitao@debian.org>

set -euo pipefail

SCRIPTDIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

source "${SCRIPTDIR}"/lib/sh/lib_netcons.sh

check_netconsole_module

modprobe netdevsim 2> /dev/null || true
rmmod netconsole 2> /dev/null || true

# Check for basic system dependency and exit if not found
# check_for_dependencies
# Set current loglevel to KERN_INFO(6), and default to KERN_NOTICE(5)
echo "6 5" > /proc/sys/kernel/printk
# Remove the namespace and network interfaces
trap do_cleanup EXIT
# Create one namespace and two interfaces
set_network

# Run the test twice, with different cmdline parameters
for BINDMODE in "ifname" "mac"
do
	echo "Running with bind mode: ${BINDMODE}" >&2
	# Create the command line for netconsole, with the configuration from
	# the function above
	CMDLINE=$(create_cmdline_str "${BINDMODE}")

	# The content of kmsg will be save to the following file
	OUTPUT_FILE="/tmp/${TARGET}-${BINDMODE}"

	# Load the module, with the cmdline set
	modprobe netconsole "${CMDLINE}"

	# Listed for netconsole port inside the namespace and destination
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
	# Unload the module
	rmmod netconsole
	echo "${BINDMODE} : Test passed" >&2
done

exit "${ksft_pass}"
