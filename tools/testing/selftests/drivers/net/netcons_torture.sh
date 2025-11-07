#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

# Repeatedly send kernel messages, toggles netconsole targets on and off,
# creates and deletes targets in parallel, and toggles the source interface to
# simulate stress conditions.
#
# This test aims to verify the robustness of netconsole under dynamic
# configurations and concurrent operations.
#
# The major goal is to run this test with LOCKDEP, Kmemleak and KASAN to make
# sure no issues is reported.
#
# Author: Breno Leitao <leitao@debian.org>

set -euo pipefail

SCRIPTDIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

source "${SCRIPTDIR}"/lib/sh/lib_netcons.sh

# Number of times the main loop run
ITERATIONS=${1:-150}

# Only test extended format
FORMAT="extended"
# And ipv6 only
IP_VERSION="ipv6"

# Create, enable and delete some targets.
create_and_delete_random_target() {
	COUNT=2
	RND_PREFIX=$(mktemp -u netcons_rnd_XXXX_)

	if [ -d "${NETCONS_CONFIGFS}/${RND_PREFIX}${COUNT}"  ] || \
	   [ -d "${NETCONS_CONFIGFS}/${RND_PREFIX}0" ]; then
		echo "Function didn't finish yet, skipping it." >&2
		return
	fi

	# enable COUNT targets
	for i in $(seq ${COUNT})
	do
		RND_TARGET="${RND_PREFIX}"${i}
		RND_TARGET_PATH="${NETCONS_CONFIGFS}"/"${RND_TARGET}"

		# Basic population so the target can come up
		_create_dynamic_target "${FORMAT}" "${RND_TARGET_PATH}"
	done

	echo "netconsole selftest: ${COUNT} additional targets were created" > /dev/kmsg
	# disable them all
	for i in $(seq ${COUNT})
	do
		RND_TARGET="${RND_PREFIX}"${i}
		RND_TARGET_PATH="${NETCONS_CONFIGFS}"/"${RND_TARGET}"
		if [[ $(cat "${RND_TARGET_PATH}/enabled") -eq 1 ]]
		then
			echo 0 > "${RND_TARGET_PATH}"/enabled
		fi
		rmdir "${RND_TARGET_PATH}"
	done
}

# Disable and enable the target mid-air, while messages
# are being transmitted.
toggle_netcons_target() {
	for i in $(seq 2)
	do
		if [ ! -d "${NETCONS_PATH}" ]
		then
			break
		fi
		echo 0 > "${NETCONS_PATH}"/enabled 2> /dev/null || true
		# Try to enable a bit harder, given it might fail to enable
		# Write to `enabled` might fail depending on the lock, which is
		# highly contentious here
		for _ in $(seq 5)
		do
			echo 1 > "${NETCONS_PATH}"/enabled 2> /dev/null || true
		done
	done
}

toggle_iface(){
	ip link set "${SRCIF}" down
	ip link set "${SRCIF}" up
}

# Start here

modprobe netdevsim 2> /dev/null || true
modprobe netconsole 2> /dev/null || true

# Check for basic system dependency and exit if not found
check_for_dependencies
# Set current loglevel to KERN_INFO(6), and default to KERN_NOTICE(5)
echo "6 5" > /proc/sys/kernel/printk
# Remove the namespace, interfaces and netconsole target on exit
trap cleanup EXIT
# Create one namespace and two interfaces
set_network "${IP_VERSION}"
# Create a dynamic target for netconsole
create_dynamic_target "${FORMAT}"

for i in $(seq "$ITERATIONS")
do
	for _ in $(seq 10)
	do
		echo "${MSG}: ${TARGET} ${i}" > /dev/kmsg
	done
	wait

	if (( i % 30 == 0 )); then
		toggle_netcons_target &
	fi

	if (( i % 50 == 0 )); then
		# create some targets, enable them, send msg and disable
		# all in a parallel thread
		create_and_delete_random_target &
	fi

	if (( i % 70 == 0 )); then
		toggle_iface &
	fi
done
wait

exit "${EXIT_STATUS}"
