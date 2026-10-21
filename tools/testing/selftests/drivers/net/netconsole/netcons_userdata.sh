#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

# Exercise the netconsole userdata payload.
#
# The first part checks that the payload the target transmits follows what
# configfs says: a value shows up in the next message, an update replaces the
# previous one, clearing the value drops the entry, and so does removing the
# key.
#
# The second part rewrites values, creates and deletes keys, and clears the
# payload entirely while messages are being sent, so the transmit path keeps
# picking up payloads that are being replaced underneath it. It runs twice,
# once with a payload small enough to fit in a single packet and once large
# enough to be fragmented.
#
# Author: Breno Leitao <leitao@debian.org>

set -euo pipefail

SCRIPTDIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

source "${SCRIPTDIR}"/../lib/sh/lib_netcons.sh

# Number of times each torture worker loops
ITERATIONS=${1:-200}

# Keys owned by each torture worker. Workers do not share keys, so a failing
# configfs operation means a real problem and not a lost race.
CHURN_KEY="churnkey"
TRANSIENT_KEY="transientkey"
# Number of keys used to push a message past MAX_PRINT_CHUNK
BULK_KEYS=8

USERDATA_DIR="${NETCONS_PATH}/userdata"
# Values are capped at MAX_EXTRADATA_VALUE_LEN(200) bytes, so ${BULK_KEYS}
# entries of this size are enough to force fragmentation
LONG_VALUE=$(printf -- 'v%.0s' {1..190})

function write_key() {
	local KEY="${1}"
	local VALUE="${2}"

	mkdir -p "${USERDATA_DIR}/${KEY}"
	echo "${VALUE}" > "${USERDATA_DIR}/${KEY}/value"
}

# Send a single message and capture it on the destination interface
function send_and_capture() {
	rm -f "${OUTPUT_FILE}"

	listen_port_and_save_to "${OUTPUT_FILE}" &
	wait_for_port "${NAMESPACE}" "${PORT}" "${IP_VERSION}"
	echo "${MSG}: ${TARGET}" > /dev/kmsg
	busywait "${BUSYWAIT_TIMEOUT}" test -s "${OUTPUT_FILE}" || true
	pkill_socat
	validate_msg "${OUTPUT_FILE}"
}

function expect_in_msg() {
	local WANTED="${1}"

	if ! grep -q -- "${WANTED}" "${OUTPUT_FILE}"; then
		echo "FAIL: '${WANTED}' not found in ${OUTPUT_FILE}" >&2
		cat "${OUTPUT_FILE}" >&2
		exit "${ksft_fail}"
	fi
}

function expect_not_in_msg() {
	local UNWANTED="${1}"

	if grep -q -- "${UNWANTED}" "${OUTPUT_FILE}"; then
		echo "FAIL: '${UNWANTED}' found in ${OUTPUT_FILE}" >&2
		cat "${OUTPUT_FILE}" >&2
		exit "${ksft_fail}"
	fi
}

# Every write publishes a new payload and frees the previous one. An empty
# value is skipped when the payload is formatted, so this also drives the
# target through having no payload at all.
function churn_value() {
	local i

	for i in $(seq "${ITERATIONS}")
	do
		echo "value${i}" > "${USERDATA_DIR}/${CHURN_KEY}/value"
		echo > "${USERDATA_DIR}/${CHURN_KEY}/value"
	done
}

# Create and delete a key underneath the sender
function churn_key() {
	local i

	for i in $(seq "${ITERATIONS}")
	do
		mkdir "${USERDATA_DIR}/${TRANSIENT_KEY}"
		echo "transient${i}" > "${USERDATA_DIR}/${TRANSIENT_KEY}/value"
		rmdir "${USERDATA_DIR}/${TRANSIENT_KEY}"
	done
}

# Keep the transmit path busy while the payload is being replaced
function send_messages() {
	local i

	for i in $(seq "${ITERATIONS}")
	do
		echo "${MSG}: ${TARGET} ${i}" > /dev/kmsg
	done
}

# Run the workers concurrently and fail if any of them hits an error
function run_workers() {
	local PIDS=()
	local WORKER
	local RET=0
	local PID

	for WORKER in "$@"
	do
		"${WORKER}" &
		PIDS+=("$!")
	done

	# Reap every worker before reporting a failure, otherwise a surviving
	# worker keeps writing to configfs while the exit trap cleans it up.
	for PID in "${PIDS[@]}"
	do
		wait "${PID}" || RET=1
	done

	if [[ "${RET}" -ne 0 ]]
	then
		echo "FAIL: userdata torture worker failed" >&2
		exit "${ksft_fail}"
	fi
}

function create_bulk_keys() {
	local i

	for i in $(seq "${BULK_KEYS}")
	do
		write_key "bulk${i}" "${LONG_VALUE}"
	done
}

function delete_bulk_keys() {
	local i

	for i in $(seq "${BULK_KEYS}")
	do
		rmdir "${USERDATA_DIR}/bulk${i}"
	done
}

# ========== #
# Start here #
# ========== #

modprobe netdevsim 2> /dev/null || true
modprobe netconsole 2> /dev/null || true

IP_VERSION="ipv4"
# The content of kmsg will be saved to the following file
OUTPUT_FILE="/tmp/${TARGET}"

# Check for basic system dependency and exit if not found
check_for_dependencies
# Set current loglevel to KERN_INFO(6), and default to KERN_NOTICE(5)
echo "6 5" > /proc/sys/kernel/printk
# Remove the namespace, interfaces and netconsole target on exit
trap cleanup EXIT
# Create one namespace and two interfaces
set_network "${IP_VERSION}"
# Create a dynamic target for netconsole
create_dynamic_target

# ===================================================
# TEST #1
# A value written to configfs reaches the destination
# ===================================================
write_key "${USERDATA_KEY}" "first"
send_and_capture
expect_in_msg "${USERDATA_KEY}=first"

# ===================================================
# TEST #2
# Updating the value replaces the previous payload
# ===================================================
write_key "${USERDATA_KEY}" "second"
send_and_capture
expect_in_msg "${USERDATA_KEY}=second"
expect_not_in_msg "${USERDATA_KEY}=first"

# ===================================================
# TEST #3
# Clearing the value drops the entry
# ===================================================
echo > "${USERDATA_DIR}/${USERDATA_KEY}/value"
send_and_capture
expect_not_in_msg "${USERDATA_KEY}="

# ===================================================
# TEST #4
# Removing the key drops the entry
# ===================================================
write_key "${USERDATA_KEY}" "third"
rmdir "${USERDATA_DIR}/${USERDATA_KEY}"
send_and_capture
expect_not_in_msg "${USERDATA_KEY}="
rm "${OUTPUT_FILE}"

# ===================================================
# TEST #5
# Torture the payload while messages are being sent,
# first unfragmented and then fragmented
# ===================================================
write_key "${CHURN_KEY}" "${USERDATA_VALUE}"
run_workers churn_value churn_key send_messages

create_bulk_keys
run_workers churn_value churn_key send_messages
delete_bulk_keys

exit "${ksft_pass}"
