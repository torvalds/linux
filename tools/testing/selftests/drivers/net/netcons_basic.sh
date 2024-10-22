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

# Simple script to test dynamic targets in netconsole
SRCIF="" # to be populated later
SRCIP=192.168.1.1
DSTIF="" # to be populated later
DSTIP=192.168.1.2

PORT="6666"
MSG="netconsole selftest"
TARGET=$(mktemp -u netcons_XXXXX)
DEFAULT_PRINTK_VALUES=$(cat /proc/sys/kernel/printk)
NETCONS_CONFIGFS="/sys/kernel/config/netconsole"
NETCONS_PATH="${NETCONS_CONFIGFS}"/"${TARGET}"
# NAMESPACE will be populated by setup_ns with a random value
NAMESPACE=""

# IDs for netdevsim
NSIM_DEV_1_ID=$((256 + RANDOM % 256))
NSIM_DEV_2_ID=$((512 + RANDOM % 256))

# Used to create and delete namespaces
source "${SCRIPTDIR}"/../../net/lib.sh
source "${SCRIPTDIR}"/../../net/net_helper.sh

# Create netdevsim interfaces
create_ifaces() {
	local NSIM_DEV_SYS_NEW=/sys/bus/netdevsim/new_device

	echo "$NSIM_DEV_2_ID" > "$NSIM_DEV_SYS_NEW"
	echo "$NSIM_DEV_1_ID" > "$NSIM_DEV_SYS_NEW"
	udevadm settle 2> /dev/null || true

	local NSIM1=/sys/bus/netdevsim/devices/netdevsim"$NSIM_DEV_1_ID"
	local NSIM2=/sys/bus/netdevsim/devices/netdevsim"$NSIM_DEV_2_ID"

	# These are global variables
	SRCIF=$(find "$NSIM1"/net -maxdepth 1 -type d ! \
		-path "$NSIM1"/net -exec basename {} \;)
	DSTIF=$(find "$NSIM2"/net -maxdepth 1 -type d ! \
		-path "$NSIM2"/net -exec basename {} \;)
}

link_ifaces() {
	local NSIM_DEV_SYS_LINK="/sys/bus/netdevsim/link_device"
	local SRCIF_IFIDX=$(cat /sys/class/net/"$SRCIF"/ifindex)
	local DSTIF_IFIDX=$(cat /sys/class/net/"$DSTIF"/ifindex)

	exec {NAMESPACE_FD}</var/run/netns/"${NAMESPACE}"
	exec {INITNS_FD}</proc/self/ns/net

	# Bind the dst interface to namespace
	ip link set "${DSTIF}" netns "${NAMESPACE}"

	# Linking one device to the other one (on the other namespace}
	if ! echo "${INITNS_FD}:$SRCIF_IFIDX $NAMESPACE_FD:$DSTIF_IFIDX"  > $NSIM_DEV_SYS_LINK
	then
		echo "linking netdevsim1 with netdevsim2 should succeed"
		cleanup
		exit "${ksft_skip}"
	fi
}

function configure_ip() {
	# Configure the IPs for both interfaces
	ip netns exec "${NAMESPACE}" ip addr add "${DSTIP}"/24 dev "${DSTIF}"
	ip netns exec "${NAMESPACE}" ip link set "${DSTIF}" up

	ip addr add "${SRCIP}"/24 dev "${SRCIF}"
	ip link set "${SRCIF}" up
}

function set_network() {
	# setup_ns function is coming from lib.sh
	setup_ns NAMESPACE

	# Create both interfaces, and assign the destination to a different
	# namespace
	create_ifaces

	# Link both interfaces back to back
	link_ifaces

	configure_ip
}

function create_dynamic_target() {
	DSTMAC=$(ip netns exec "${NAMESPACE}" \
		 ip link show "${DSTIF}" | awk '/ether/ {print $2}')

	# Create a dynamic target
	mkdir "${NETCONS_PATH}"

	echo "${DSTIP}" > "${NETCONS_PATH}"/remote_ip
	echo "${SRCIP}" > "${NETCONS_PATH}"/local_ip
	echo "${DSTMAC}" > "${NETCONS_PATH}"/remote_mac
	echo "${SRCIF}" > "${NETCONS_PATH}"/dev_name

	echo 1 > "${NETCONS_PATH}"/enabled
}

function cleanup() {
	local NSIM_DEV_SYS_DEL="/sys/bus/netdevsim/del_device"

	# delete netconsole dynamic reconfiguration
	echo 0 > "${NETCONS_PATH}"/enabled
	# Remove the configfs entry
	rmdir "${NETCONS_PATH}"

	# Delete netdevsim devices
	echo "$NSIM_DEV_2_ID" > "$NSIM_DEV_SYS_DEL"
	echo "$NSIM_DEV_1_ID" > "$NSIM_DEV_SYS_DEL"

	# this is coming from lib.sh
	cleanup_all_ns

	# Restoring printk configurations
	echo "${DEFAULT_PRINTK_VALUES}" > /proc/sys/kernel/printk
}

function listen_port_and_save_to() {
	local OUTPUT=${1}
	# Just wait for 2 seconds
	timeout 2 ip netns exec "${NAMESPACE}" \
		socat UDP-LISTEN:"${PORT}",fork "${OUTPUT}"
}

function validate_result() {
	local TMPFILENAME="$1"

	# Check if the file exists
	if [ ! -f "$TMPFILENAME" ]; then
		echo "FAIL: File was not generated." >&2
		exit "${ksft_fail}"
	fi

	if ! grep -q "${MSG}" "${TMPFILENAME}"; then
		echo "FAIL: ${MSG} not found in ${TMPFILENAME}" >&2
		cat "${TMPFILENAME}" >&2
		exit "${ksft_fail}"
	fi

	# Delete the file once it is validated, otherwise keep it
	# for debugging purposes
	rm "${TMPFILENAME}"
	exit "${ksft_pass}"
}

function check_for_dependencies() {
	if [ "$(id -u)" -ne 0 ]; then
		echo "This test must be run as root" >&2
		exit "${ksft_skip}"
	fi

	if ! which socat > /dev/null ; then
		echo "SKIP: socat(1) is not available" >&2
		exit "${ksft_skip}"
	fi

	if ! which ip > /dev/null ; then
		echo "SKIP: ip(1) is not available" >&2
		exit "${ksft_skip}"
	fi

	if ! which udevadm > /dev/null ; then
		echo "SKIP: udevadm(1) is not available" >&2
		exit "${ksft_skip}"
	fi

	if [ ! -d "${NETCONS_CONFIGFS}" ]; then
		echo "SKIP: directory ${NETCONS_CONFIGFS} does not exist. Check if NETCONSOLE_DYNAMIC is enabled" >&2
		exit "${ksft_skip}"
	fi

	if ip link show "${DSTIF}" 2> /dev/null; then
		echo "SKIP: interface ${DSTIF} exists in the system. Not overwriting it." >&2
		exit "${ksft_skip}"
	fi

	if ip addr list | grep -E "inet.*(${SRCIP}|${DSTIP})" 2> /dev/null; then
		echo "SKIP: IPs already in use. Skipping it" >&2
		exit "${ksft_skip}"
	fi
}

# ========== #
# Start here #
# ========== #
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
