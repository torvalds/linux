#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

# This file contains functions and helpers to support the netconsole
# selftests
#
# Author: Breno Leitao <leitao@debian.org>

set -euo pipefail

LIBDIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

SRCIF="" # to be populated later
SRCIP="" # to be populated later
SRCIP4="192.0.2.1"
SRCIP6="fc00::1"
DSTIF="" # to be populated later
DSTIP="" # to be populated later
DSTIP4="192.0.2.2"
DSTIP6="fc00::2"

PORT="6666"
MSG="netconsole selftest"
USERDATA_KEY="key"
USERDATA_VALUE="value"
TARGET=$(mktemp -u netcons_XXXXX)
DEFAULT_PRINTK_VALUES=$(cat /proc/sys/kernel/printk)
NETCONS_CONFIGFS="/sys/kernel/config/netconsole"
NETCONS_PATH="${NETCONS_CONFIGFS}"/"${TARGET}"
# NAMESPACE will be populated by setup_ns with a random value
NAMESPACE=""

# IDs for netdevsim. We either use NSIM_DEV_{1,2}_ID for standard test
# or NSIM_BOND_{T,R}X_{1,2} for the bonding tests. Not both at the
# same time.
NSIM_DEV_1_ID=$((256 + RANDOM % 256))
NSIM_DEV_2_ID=$((512 + RANDOM % 256))
NSIM_BOND_TX_1=$((768 + RANDOM % 256))
NSIM_BOND_TX_2=$((1024 + RANDOM % 256))
NSIM_BOND_RX_1=$((1280 + RANDOM % 256))
NSIM_BOND_RX_2=$((1536 + RANDOM % 256))
NSIM_DEV_SYS_NEW="/sys/bus/netdevsim/new_device"
NSIM_DEV_SYS_LINK="/sys/bus/netdevsim/link_device"

# Used to create and delete namespaces
source "${LIBDIR}"/../../../../net/lib.sh

# Create netdevsim interfaces
create_ifaces() {
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

function select_ipv4_or_ipv6()
{
	local VERSION=${1}

	if [[ "$VERSION" == "ipv6" ]]
	then
		DSTIP="${DSTIP6}"
		SRCIP="${SRCIP6}"
	else
		DSTIP="${DSTIP4}"
		SRCIP="${SRCIP4}"
	fi
}

function set_network() {
	local IP_VERSION=${1:-"ipv4"}

	# setup_ns function is coming from lib.sh
	setup_ns NAMESPACE

	# Create both interfaces, and assign the destination to a different
	# namespace
	create_ifaces

	# Link both interfaces back to back
	link_ifaces

	select_ipv4_or_ipv6 "${IP_VERSION}"
	configure_ip
}

function _create_dynamic_target() {
	local FORMAT="${1:?FORMAT parameter required}"
	local NCPATH="${2:?NCPATH parameter required}"

	DSTMAC=$(ip netns exec "${NAMESPACE}" \
		 ip link show "${DSTIF}" | awk '/ether/ {print $2}')

	# Create a dynamic target
	mkdir "${NCPATH}"

	echo "${DSTIP}" > "${NCPATH}"/remote_ip
	echo "${SRCIP}" > "${NCPATH}"/local_ip
	echo "${DSTMAC}" > "${NCPATH}"/remote_mac
	echo "${SRCIF}" > "${NCPATH}"/dev_name

	if [ "${FORMAT}" == "basic" ]
	then
		# Basic target does not support release
		echo 0 > "${NCPATH}"/release
		echo 0 > "${NCPATH}"/extended
	elif [ "${FORMAT}" == "extended" ]
	then
		echo 1 > "${NCPATH}"/extended
	fi
}

function create_dynamic_target() {
	local FORMAT=${1:-"extended"}
	local NCPATH=${2:-"$NETCONS_PATH"}
	_create_dynamic_target "${FORMAT}" "${NCPATH}"

	echo 1 > "${NCPATH}"/enabled

	# This will make sure that the kernel was able to
	# load the netconsole driver configuration. The console message
	# gets more organized/sequential as well.
	sleep 1
}

# Generate the command line argument for netconsole following:
#  netconsole=[+][src-port]@[src-ip]/[<dev>],[tgt-port]@<tgt-ip>/[tgt-macaddr]
function create_cmdline_str() {
	local BINDMODE=${1:-"ifname"}
	if [ "${BINDMODE}" == "ifname" ]
	then
		SRCDEV=${SRCIF}
	else
		SRCDEV=$(mac_get "${SRCIF}")
	fi

	DSTMAC=$(ip netns exec "${NAMESPACE}" \
		 ip link show "${DSTIF}" | awk '/ether/ {print $2}')
	SRCPORT="1514"
	TGTPORT="6666"

	echo "netconsole=\"+${SRCPORT}@${SRCIP}/${SRCDEV},${TGTPORT}@${DSTIP}/${DSTMAC}\""
}

# Do not append the release to the header of the message
function disable_release_append() {
	echo 0 > "${NETCONS_PATH}"/enabled
	echo 0 > "${NETCONS_PATH}"/release
	echo 1 > "${NETCONS_PATH}"/enabled
}

function do_cleanup() {
	local NSIM_DEV_SYS_DEL="/sys/bus/netdevsim/del_device"

	# Delete netdevsim devices
	echo "$NSIM_DEV_2_ID" > "$NSIM_DEV_SYS_DEL"
	echo "$NSIM_DEV_1_ID" > "$NSIM_DEV_SYS_DEL"

	# this is coming from lib.sh
	cleanup_all_ns

	# Restoring printk configurations
	echo "${DEFAULT_PRINTK_VALUES}" > /proc/sys/kernel/printk
}

function cleanup_netcons() {
	# delete netconsole dynamic reconfiguration
	# do not fail if the target is already disabled
	if [[ ! -d "${NETCONS_PATH}" ]]
	then
		# in some cases this is called before netcons path is created
		return
	fi
	if [[ $(cat "${NETCONS_PATH}"/enabled) != 0 ]]
	then
		echo 0 > "${NETCONS_PATH}"/enabled || true
	fi
	# Remove all the keys that got created during the selftest
	find "${NETCONS_PATH}/userdata/" -mindepth 1 -type d -delete
	# Remove the configfs entry
	rmdir "${NETCONS_PATH}"
}

function cleanup() {
	cleanup_netcons
	do_cleanup
}

function set_user_data() {
	if [[ ! -d "${NETCONS_PATH}""/userdata" ]]
	then
		echo "Userdata path not available in ${NETCONS_PATH}/userdata"
		exit "${ksft_skip}"
	fi

	KEY_PATH="${NETCONS_PATH}/userdata/${USERDATA_KEY}"
	mkdir -p "${KEY_PATH}"
	VALUE_PATH="${KEY_PATH}""/value"
	echo "${USERDATA_VALUE}" > "${VALUE_PATH}"
}

function listen_port_and_save_to() {
	local OUTPUT=${1}
	local IPVERSION=${2:-"ipv4"}

	if [ "${IPVERSION}" == "ipv4" ]
	then
		SOCAT_MODE="UDP-LISTEN"
	else
		SOCAT_MODE="UDP6-LISTEN"
	fi

	# Just wait for 2 seconds
	timeout 2 ip netns exec "${NAMESPACE}" \
		socat "${SOCAT_MODE}":"${PORT}",fork "${OUTPUT}"
}

# Only validate that the message arrived properly
function validate_msg() {
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
}

# Validate the message and userdata
function validate_result() {
	local TMPFILENAME="$1"

	# TMPFILENAME will contain something like:
	# 6.11.1-0_fbk0_rc13_509_g30d75cea12f7,13,1822,115075213798,-;netconsole selftest: netcons_gtJHM
	#  key=value

	validate_msg "${TMPFILENAME}"

	# userdata is not supported on basic format target,
	# thus, do not validate it.
	if [ "${FORMAT}" != "basic" ];
	then
		if ! grep -q "${USERDATA_KEY}=${USERDATA_VALUE}" "${TMPFILENAME}"; then
			echo "FAIL: ${USERDATA_KEY}=${USERDATA_VALUE} not found in ${TMPFILENAME}" >&2
			cat "${TMPFILENAME}" >&2
			exit "${ksft_fail}"
		fi
	fi

	# Delete the file once it is validated, otherwise keep it
	# for debugging purposes
	rm "${TMPFILENAME}"
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

	if [ ! -f /proc/net/if_inet6 ]; then
		echo "SKIP: IPv6 not configured. Check if CONFIG_IPV6 is enabled" >&2
		exit "${ksft_skip}"
	fi

	if [ ! -f "${NSIM_DEV_SYS_NEW}" ]; then
		echo "SKIP: file ${NSIM_DEV_SYS_NEW} does not exist. Check if CONFIG_NETDEVSIM is enabled" >&2
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

	REGEXP4="inet.*(${SRCIP4}|${DSTIP4})"
	REGEXP6="inet.*(${SRCIP6}|${DSTIP6})"
	if ip addr list | grep -E "${REGEXP4}" 2> /dev/null; then
		echo "SKIP: IPv4s already in use. Skipping it" >&2
		exit "${ksft_skip}"
	fi

	if ip addr list | grep -E "${REGEXP6}" 2> /dev/null; then
		echo "SKIP: IPv6s already in use. Skipping it" >&2
		exit "${ksft_skip}"
	fi
}

function check_for_taskset() {
	if ! which taskset > /dev/null ; then
		echo "SKIP: taskset(1) is not available" >&2
		exit "${ksft_skip}"
	fi
}

# This is necessary if running multiple tests in a row
function pkill_socat() {
	PROCESS_NAME4="socat UDP-LISTEN:6666,fork ${OUTPUT_FILE}"
	PROCESS_NAME6="socat UDP6-LISTEN:6666,fork ${OUTPUT_FILE}"
	# socat runs under timeout(1), kill it if it is still alive
	# do not fail if socat doesn't exist anymore
	set +e
	pkill -f "${PROCESS_NAME4}"
	pkill -f "${PROCESS_NAME6}"
	set -e
}

# Check if netconsole was compiled as a module, otherwise exit
function check_netconsole_module() {
	if modinfo netconsole | grep filename: | grep -q builtin
	then
		echo "SKIP: netconsole should be compiled as a module" >&2
		exit "${ksft_skip}"
	fi
}

# A wrapper to translate protocol version to udp version
function wait_for_port() {
	local NAMESPACE=${1}
	local PORT=${2}
	IP_VERSION=${3}

	if [ "${IP_VERSION}" == "ipv6" ]
	then
		PROTOCOL="udp6"
	else
		PROTOCOL="udp"
	fi

	wait_local_port_listen "${NAMESPACE}" "${PORT}" "${PROTOCOL}"
	# even after the port is open, let's wait 1 second before writing
	# otherwise the packet could be missed, and the test will fail. Happens
	# more frequently on IPv6
	sleep 1
}

# Clean up netdevsim ifaces created for bonding test
function cleanup_bond_nsim() {
	ip -n "${TXNS}" \
		link delete "${BOND_TX_MAIN_IF}" type bond || true
	ip -n "${RXNS}" \
		link delete "${BOND_RX_MAIN_IF}" type bond || true

	cleanup_netdevsim "$NSIM_BOND_TX_1"
	cleanup_netdevsim "$NSIM_BOND_TX_2"
	cleanup_netdevsim "$NSIM_BOND_RX_1"
	cleanup_netdevsim "$NSIM_BOND_RX_2"
}

# cleanup tests that use bonding interfaces
function cleanup_bond() {
	cleanup_netcons
	cleanup_bond_nsim
	cleanup_all_ns
	ip link delete "${VETH0}" || true
}
