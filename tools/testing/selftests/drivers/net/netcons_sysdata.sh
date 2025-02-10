#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

# A test that makes sure that sysdata runtime CPU data is properly set
# when a message is sent.
#
# There are 3 different tests, every time sent using a random CPU.
#  - Test #1
#    * Only enable cpu_nr sysdata feature.
#  - Test #2
#    * Keep cpu_nr sysdata feature enable and enable userdata.
#  - Test #3
#    * keep userdata enabled, and disable sysdata cpu_nr feature.
#
# Author: Breno Leitao <leitao@debian.org>

set -euo pipefail

SCRIPTDIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

source "${SCRIPTDIR}"/lib/sh/lib_netcons.sh

# Enable the sysdata cpu_nr feature
function set_cpu_nr() {
	if [[ ! -f "${NETCONS_PATH}/userdata/cpu_nr_enabled" ]]
	then
		echo "Populate CPU configfs path not available in ${NETCONS_PATH}/userdata/cpu_nr_enabled" >&2
		exit "${ksft_skip}"
	fi

	echo 1 > "${NETCONS_PATH}/userdata/cpu_nr_enabled"
}

# Disable the sysdata cpu_nr feature
function unset_cpu_nr() {
	echo 0 > "${NETCONS_PATH}/userdata/cpu_nr_enabled"
}

# Test if MSG content and `cpu=${CPU}` exists in OUTPUT_FILE
function validate_sysdata_cpu_exists() {
	# OUTPUT_FILE will contain something like:
	# 6.11.1-0_fbk0_rc13_509_g30d75cea12f7,13,1822,115075213798,-;netconsole selftest: netcons_gtJHM
	#  userdatakey=userdatavalue
	#  cpu=X

	if [ ! -f "$OUTPUT_FILE" ]; then
		echo "FAIL: File was not generated." >&2
		exit "${ksft_fail}"
	fi

	if ! grep -q "${MSG}" "${OUTPUT_FILE}"; then
		echo "FAIL: ${MSG} not found in ${OUTPUT_FILE}" >&2
		cat "${OUTPUT_FILE}" >&2
		exit "${ksft_fail}"
	fi

	# Check if cpu=XX exists in the file and matches the one used
	# in taskset(1)
	if ! grep -q "cpu=${CPU}\+" "${OUTPUT_FILE}"; then
		echo "FAIL: 'cpu=${CPU}' not found in ${OUTPUT_FILE}" >&2
		cat "${OUTPUT_FILE}" >&2
		exit "${ksft_fail}"
	fi

	rm "${OUTPUT_FILE}"
	pkill_socat
}

# Test if MSG content exists in OUTPUT_FILE but no `cpu=` string
function validate_sysdata_no_cpu() {
	if [ ! -f "$OUTPUT_FILE" ]; then
		echo "FAIL: File was not generated." >&2
		exit "${ksft_fail}"
	fi

	if ! grep -q "${MSG}" "${OUTPUT_FILE}"; then
		echo "FAIL: ${MSG} not found in ${OUTPUT_FILE}" >&2
		cat "${OUTPUT_FILE}" >&2
		exit "${ksft_fail}"
	fi

	if grep -q "cpu=" "${OUTPUT_FILE}"; then
		echo "FAIL: 'cpu=  found in ${OUTPUT_FILE}" >&2
		cat "${OUTPUT_FILE}" >&2
		exit "${ksft_fail}"
	fi

	rm "${OUTPUT_FILE}"
}

# Start socat, send the message and wait for the file to show up in the file
# system
function runtest {
	# Listen for netconsole port inside the namespace and destination
	# interface
	listen_port_and_save_to "${OUTPUT_FILE}" &
	# Wait for socat to start and listen to the port.
	wait_local_port_listen "${NAMESPACE}" "${PORT}" udp
	# Send the message
	taskset -c "${CPU}" echo "${MSG}: ${TARGET}" > /dev/kmsg
	# Wait until socat saves the file to disk
	busywait "${BUSYWAIT_TIMEOUT}" test -s "${OUTPUT_FILE}"
}

# ========== #
# Start here #
# ========== #

modprobe netdevsim 2> /dev/null || true
modprobe netconsole 2> /dev/null || true

# Check for basic system dependency and exit if not found
check_for_dependencies
# This test also depends on taskset(1). Check for it before starting the test
check_for_taskset

# Set current loglevel to KERN_INFO(6), and default to KERN_NOTICE(5)
echo "6 5" > /proc/sys/kernel/printk
# Remove the namespace, interfaces and netconsole target on exit
trap cleanup EXIT
# Create one namespace and two interfaces
set_network
# Create a dynamic target for netconsole
create_dynamic_target

#====================================================
# TEST #1
# Send message from a random CPU
#====================================================
# Random CPU in the system
CPU=$((RANDOM % $(nproc)))
OUTPUT_FILE="/tmp/${TARGET}_1"
MSG="Test #1 from CPU${CPU}"
# Enable the auto population of cpu_nr
set_cpu_nr
runtest
# Make sure the message was received in the dst part
# and exit
validate_sysdata_cpu_exists

#====================================================
# TEST #2
# This test now adds userdata together with sysdata
# ===================================================
# Get a new random CPU
CPU=$((RANDOM % $(nproc)))
OUTPUT_FILE="/tmp/${TARGET}_2"
MSG="Test #2 from CPU${CPU}"
set_user_data
runtest
validate_sysdata_cpu_exists

# ===================================================
# TEST #3
# Unset cpu_nr, so, no CPU should be appended.
# userdata is still set
# ===================================================
CPU=$((RANDOM % $(nproc)))
OUTPUT_FILE="/tmp/${TARGET}_3"
MSG="Test #3 from CPU${CPU}"
# Enable the auto population of cpu_nr
unset_cpu_nr
runtest
# At this time, cpu= shouldn't be present in the msg
validate_sysdata_no_cpu

exit "${ksft_pass}"
