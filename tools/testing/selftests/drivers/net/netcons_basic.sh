#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

# This test creates two netdevsim virtual interfaces, assigns one of them (the
# "destination interface") to a new namespace, and assigns IP addresses to both
# interfaces.
#
# It listens on the destination interface using socat and configures a dynamic
# target on netconsole, pointing to the destination IP address.
#
# Finally, it checks whether the message was received properly on the
# destination interface.  Note that this test may pollute the kernel log buffer
# (dmesg) and relies on dynamic configuration and namespaces being configured.
#
# Author: Breno Leitao <leitao@debian.org>

set -euo pipefail

SCRIPTDIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

source "${SCRIPTDIR}"/lib/sh/lib_netcons.sh

modprobe netdevsim 2> /dev/null || true
modprobe netconsole 2> /dev/null || true

# The content of kmsg will be save to the following file
OUTPUT_FILE="/tmp/${TARGET}"

# Check for basic system dependency and exit if not found
check_for_dependencies
# Set current loglevel to KERN_INFO(6), and default to KERN_NOTICE(5)
echo "6 5" > /proc/sys/kernel/printk
# Remove the namespace, interfaces and netconsole target on exit
trap cleanup EXIT
# Create one namespace and two interfaces
set_network
# Create a dynamic target for netconsole
create_dynamic_target
# Set userdata "key" with the "value" value
set_user_data
# Listed for netconsole port inside the namespace and destination interface
listen_port_and_save_to "${OUTPUT_FILE}" &
# Wait for socat to start and listen to the port.
wait_local_port_listen "${NAMESPACE}" "${PORT}" udp
# Send the message
echo "${MSG}: ${TARGET}" > /dev/kmsg
# Wait until socat saves the file to disk
busywait "${BUSYWAIT_TIMEOUT}" test -s "${OUTPUT_FILE}"

# Make sure the message was received in the dst part
# and exit
validate_result "${OUTPUT_FILE}"
