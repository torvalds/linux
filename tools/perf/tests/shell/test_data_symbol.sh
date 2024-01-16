#!/bin/bash
# Test data symbol

# SPDX-License-Identifier: GPL-2.0
# Leo Yan <leo.yan@linaro.org>, 2022

skip_if_no_mem_event() {
	perf mem record -e list 2>&1 | egrep -q 'available' && return 0
	return 2
}

skip_if_no_mem_event || exit 2

# skip if there's no compiler
if ! [ -x "$(command -v cc)" ]; then
	echo "skip: no compiler, install gcc"
	exit 2
fi

TEST_PROGRAM=$(mktemp /tmp/__perf_test.program.XXXXX)
PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXX)

check_result() {
	# The memory report format is as below:
	#    99.92%  ...  [.] buf1+0x38
	result=$(perf mem report -i ${PERF_DATA} -s symbol_daddr -q 2>&1 |
		 awk '/buf1/ { print $4 }')

	# Testing is failed if has no any sample for "buf1"
	[ -z "$result" ] && return 1

	while IFS= read -r line; do
		# The "data1" and "data2" fields in structure "buf1" have
		# offset "0x0" and "0x38", returns failure if detect any
		# other offset value.
		if [ "$line" != "buf1+0x0" ] && [ "$line" != "buf1+0x38" ]; then
			return 1
		fi
	done <<< "$result"

	return 0
}

cleanup_files()
{
	echo "Cleaning up files..."
	rm -f ${PERF_DATA}
	rm -f ${TEST_PROGRAM}
}

trap cleanup_files exit term int

# compile test program
echo "Compiling test program..."
cat << EOF | cc -o ${TEST_PROGRAM} -x c -
typedef struct _buf {
	char data1;
	char reserved[55];
	char data2;
} buf __attribute__((aligned(64)));

static buf buf1;

int main(void) {
	for (;;) {
		buf1.data1++;
		buf1.data2 += buf1.data1;
	}
	return 0;
}
EOF

echo "Recording workload..."

# perf mem/c2c internally uses IBS PMU on AMD CPU which doesn't support
# user/kernel filtering and per-process monitoring, spin program on
# specific CPU and test in per-CPU mode.
is_amd=$(egrep -c 'vendor_id.*AuthenticAMD' /proc/cpuinfo)
if (($is_amd >= 1)); then
	perf mem record -o ${PERF_DATA} -C 0 -- taskset -c 0 $TEST_PROGRAM &
else
	perf mem record --all-user -o ${PERF_DATA} -- $TEST_PROGRAM &
fi

PERFPID=$!

sleep 1

kill $PERFPID
wait $PERFPID

check_result
exit $?
