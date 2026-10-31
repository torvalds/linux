#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# perf report hybrid merge tests

set -e

err=0
log_dir=$(mktemp -d /tmp/__perf_test.report_hybrid_merge.XXXXXX)
perf_data="${log_dir}/perf.data"
perf_out="${log_dir}/perf.out"
perf_config="${log_dir}/perfconfig"

cleanup() {
	rm -rf "${log_dir}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

# Record 2 events so that all the events of a hybrid machine must be merged,
# not just the first pair found.
events=""
record_events() {
	for try in "cycles,instructions" "cpu-clock,task-clock"; do
		if perf record -o "${perf_data}" -e "${try}" -- \
		   perf test -w thloop 1 >/dev/null 2>&1; then
			events="${try//,/ }"
			return 0
		fi
	done
	return 1
}

test_hybrid_merge_report() {
	echo "Perf report hybrid merge test"

	# Test with multiple fields to ensure formatting alignment doesn't hide members
	if ! perf report -i "${perf_data}" --hybrid-merge \
			 -F comm,overhead --stdio > "${perf_out}" 2>&1; then
		echo "Perf report hybrid merge test [Failed: run err]"
		err=1
		return
	fi

	# Check if the output actually contains the 'comm' and 'overhead' headers correctly
	if ! grep -qi "Overhead" "${perf_out}"; then
		echo "Perf report hybrid merge test [Failed: missing overhead header]"
		err=1
		return
	fi

	if ! grep -qi "Command" "${perf_out}"; then
		echo "Perf report hybrid merge test [Failed: missing comm header]"
		err=1
		return
	fi

	# Every recorded event must be displayed, merged into a group on a
	# hybrid machine and separately elsewhere.
	if ! perf report -i "${perf_data}" --hybrid-merge --stdio \
			 > "${perf_out}" 2>&1; then
		echo "Perf report hybrid merge test [Failed: run err]"
		err=1
		return
	fi
	for event in ${events}; do
		if ! grep -q -- "${event}" "${perf_out}"; then
			echo "Perf report hybrid merge test [Failed: missing event ${event}]"
			cat "${perf_out}"
			err=1
			return
		fi
	done

	echo "Perf report hybrid merge test [Success]"
}

test_hybrid_merge_config() {
	echo "Perf report hybrid merge config test"

	cat <<EOF > "${perf_config}"
[core]
	hybrid-merge = true
EOF

	# Merging from the config file is a default, it must give way to an
	# explicitly requested --hierarchy rather than failing.
	if ! PERF_CONFIG="${perf_config}" perf report -i "${perf_data}" \
	     --hierarchy --stdio > "${perf_out}" 2>&1; then
		echo "Perf report hybrid merge config test [Failed: --hierarchy]"
		cat "${perf_out}"
		err=1
		return
	fi

	# Asking for both on the command line remains an error.
	if PERF_CONFIG="${perf_config}" perf report -i "${perf_data}" \
	   --hierarchy --hybrid-merge --stdio > "${perf_out}" 2>&1; then
		echo "Perf report hybrid merge config test [Failed: no error for both]"
		err=1
		return
	fi

	echo "Perf report hybrid merge config test [Success]"
}

if ! record_events; then
	echo "Perf report hybrid merge test [Skipped: perf record failed]"
	cleanup
	exit 2
fi

test_hybrid_merge_report
test_hybrid_merge_config
cleanup
exit $err
