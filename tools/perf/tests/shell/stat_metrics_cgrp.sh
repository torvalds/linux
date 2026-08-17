#!/bin/bash
# perf stat metrics --for-each-cgroup test
# SPDX-License-Identifier: GPL-2.0

set -e

test_cgroups=

log_verbose() {
	echo "$1"
}

is_numeric_and_non_zero() {
	local val="$1"
	if [[ "${val}" =~ ^[0-9]+$ ]] && [ "${val}" -gt 0 ]
	then
		return 0 # True
	fi
	return 1 # False
}

# skip if system-wide is not supported
check_system_wide()
{
	log_verbose "Checking system-wide..."
	if ! perf stat -a --metrics=insn_per_cycle sleep 0.01 > /dev/null 2>&1
	then
		log_verbose "Skipping: system-wide monitoring not supported"
		exit 2
	fi
}

# find two cgroups to measure
find_cgroups()
{
	log_verbose "Finding cgroups..."
	# try usual systemd slices first
	if [ -d /sys/fs/cgroup/system.slice ] && [ -d /sys/fs/cgroup/user.slice ]
	then
		test_cgroups="system.slice,user.slice"
		log_verbose "Found cgroups: ${test_cgroups}"
		return
	fi

	# try root and self cgroups
	find_cgroups_self_cgrp=$(grep perf_event /proc/self/cgroup | cut -d: -f3)
	if [ -z "${find_cgroups_self_cgrp}" ]
	then
		# cgroup v2 doesn't specify perf_event
		find_cgroups_self_cgrp=$(grep ^0: /proc/self/cgroup | cut -d: -f3)
	fi

	if [ -z "${find_cgroups_self_cgrp}" ]
	then
		test_cgroups="/"
	else
		test_cgroups="/,${find_cgroups_self_cgrp}"
	fi
	log_verbose "Found cgroups: ${test_cgroups}"
}

# Check if metric is reported for each cgroup
# $1: extra options (e.g. --bpf-counters)
check_metric_reported()
{
	local opts="$1"
	local output

	log_verbose "Running check_metric_reported with opts '${opts}'..."
	# Run perf stat
	if ! output=$(perf stat -a ${opts} \
			--metrics=insn_per_cycle \
			--for-each-cgroup "${test_cgroups}" \
			-x, sleep 0.1 2>&1)
	then
		echo "FAIL: perf stat failed with exit code $?"
		echo "Output: ${output}"
		exit 1
	fi

	log_verbose "perf stat output:"
	log_verbose "${output}"

	# Split test_cgroups by comma
	IFS=',' read -r -a cgrps <<< "${test_cgroups}"

	for cgrp in "${cgrps[@]}"; do
		# Find metric lines for this cgroup
		# We use exact cgroup match with surrounding commas
		local cgrp_lines
		cgrp_lines=$(echo "${output}" | grep -F ",${cgrp}," | grep "insn_per_cycle" || true)

		if [ -z "${cgrp_lines}" ]
		then
			echo "FAIL: No metric lines found for cgroup '${cgrp}'"
			exit 1
		fi

		# Parse each metric line
		while read -r line; do
			[ -z "${line}" ] && continue

			local val1
			val1=$(echo "${line}" | cut -d, -f1)

			local event_name
			event_name=$(echo "${line}" | cut -d, -f3)

			local cycles_val=""
			local inst_val=""

			if echo "${event_name}" | grep -q -i "cycles"
			then
				cycles_val="${val1}"
				# Find corresponding instructions event
				local inst_event_name
				inst_event_name="${event_name/cpu-cycles/instructions}"
				inst_event_name="${inst_event_name/cycles/instructions}"

				local inst_line
				inst_line=$(echo "${output}" | \
					grep -F ",${cgrp}," | \
					grep -F "${inst_event_name}" || true)
				inst_val=$(echo "${inst_line}" | cut -d, -f1)
			elif echo "${event_name}" | grep -q -i "instructions"
			then
				inst_val="${val1}"
				# Find corresponding cycles event (try cpu-cycles
				# first, then cycles)
				local cycles_event_name
				cycles_event_name="${event_name/instructions/cpu-cycles}"
				local cycles_line
				cycles_line=$(echo "${output}" | \
					grep -F ",${cgrp}," | \
					grep -F "${cycles_event_name}" || true)

				if [ -z "${cycles_line}" ]
				then
					# Try "cycles" instead of "cpu-cycles"
					cycles_event_name="${event_name/instructions/cycles}"
					cycles_line=$(echo "${output}" | \
						grep -F ",${cgrp}," | \
						grep -F "${cycles_event_name}" || true)
				fi
				cycles_val=$(echo "${cycles_line}" | cut -d, -f1)
			fi

			log_verbose "Cgroup '${cgrp}': event '${event_name}' \
val '${cycles_val}', inst val '${inst_val}'"

			# Only enforce metric check if both cycles and
			# instructions have non-zero numeric counts
			if is_numeric_and_non_zero "${cycles_val}" && \
			   is_numeric_and_non_zero "${inst_val}"
			then
				log_verbose "Enforcing metric check for cgroup '${cgrp}' \
event '${event_name}'"
				# Check for nan or nested in the metric value (7th field)
				# Actually we can just check the whole line for simplicity
				if echo "${line}" | grep -q -i -E ",nan,|,nested,"
				then
					echo "FAIL: Invalid metric value (nan/nested) \
for cgroup '${cgrp}'"
					echo "Line: ${line}"
					exit 1
				fi
				# Check for empty metric value (2 consecutive
				# commas before the unit)
				if echo "${line}" | grep -q -E ",,[[:space:]]*[^,]*insn_per_cycle"
				then
					echo "FAIL: Empty metric value for cgroup '${cgrp}'"
					echo "Line: ${line}"
					exit 1
				fi
			else
				log_verbose "Skipping metric check for cgroup '${cgrp}' \
event '${event_name}' (idle or not counted)"
			fi
		done <<< "${cgrp_lines}"
	done
	log_verbose "check_metric_reported passed for opts '${opts}'"
}

check_system_wide
find_cgroups

# Test 1: Without BPF counters
check_metric_reported ""

# Test 2: With BPF counters (if supported)
log_verbose "Checking BPF support..."
if perf stat -a --bpf-counters --for-each-cgroup / true > /dev/null 2>&1
then
	log_verbose "BPF supported, running Test 2..."
	check_metric_reported "--bpf-counters"
else
	log_verbose "BPF not supported, skipping Test 2"
fi

exit 0
