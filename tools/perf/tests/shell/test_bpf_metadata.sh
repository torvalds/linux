#!/bin/bash
# BPF metadata collection test
#
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)

cleanup() {
	rm -f "${perfdata}"
	rm -f "${perfdata}".old
	trap - EXIT TERM INT
}

trap_cleanup() {
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

test_bpf_metadata() {
	echo "Checking BPF metadata collection"

	if ! perf check -q feature libbpf-strings ; then
		echo "Basic BPF metadata test [skipping - not supported]"
		err=0
		return
	fi

	# This is a basic invocation of perf record
	# that invokes the perf_sample_filter BPF program.
	if ! perf record -e task-clock --filter 'ip > 0' \
			 -o "${perfdata}" sleep 1 2> /dev/null
	then
		echo "Basic BPF metadata test [Failed record]"
		err=1
		return
	fi

	# The BPF programs that ship with "perf" all have the following
	# variable defined at compile time:
	#
	#   const char bpf_metadata_perf_version[] SEC(".rodata") = <...>;
	#
	# This invocation looks for a PERF_RECORD_BPF_METADATA event,
	# and checks that its content contains the string given by
	# "perf version".
	VERS=$(perf version | awk '{print $NF}')
	if ! perf script --show-bpf-events -i "${perfdata}" | awk '
		/PERF_RECORD_BPF_METADATA.*perf_sample_filter/ {
			header = 1;
		}
		/^ *entry/ {
			if (header) { header = 0; entry = 1; }
		}
		$0 !~ /^ *entry/ {
			entry = 0;
		}
		/perf_version/ {
			if (entry) print $NF;
		}
	' | grep -qF "$VERS"
	then
		echo "Basic BPF metadata test [Failed invalid output]"
		err=1
		return
	fi
	echo "Basic BPF metadata test [Success]"
}

test_bpf_metadata

cleanup
exit $err
