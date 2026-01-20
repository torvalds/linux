#!/bin/bash
# Perf event open fallback test
# SPDX-License-Identifier: GPL-2.0

skip_cnt=0
ok_cnt=0
err_cnt=0

perf_record()
{
	perf record -o /dev/null "$@" -- true 1>/dev/null 2>&1
}

test_decrease_precise_ip()
{
	echo "Decrease precise ip test"

	perf list pmu | grep -q 'cycles' || return 2

	if ! perf_record -e cycles; then
		return 2
	fi

	# It should reduce precision level down to 0 if needed.
	if ! perf_record -e cycles:P; then
		return 1
	fi
	return 0
}

test_decrease_precise_ip_complicated()
{
	echo "Decrease precise ip test (complicated case)"

	perf list pmu | grep -q 'mem-loads-aux' || return 2

	if ! perf_record -e '{mem-loads-aux:S,mem-loads:PS}'; then
		return 1
	fi
	return 0
}

count_result()
{
	if [ "$1" -eq 2 ] ; then
		skip_cnt=$((skip_cnt + 1))
		return
	fi
	if [ "$1" -eq 0 ] ; then
		ok_cnt=$((ok_cnt + 1))
		return
	fi
	err_cnt=$((err_cnt + 1))
}

ret=0
test_decrease_precise_ip		|| ret=$? ; count_result $ret ; ret=0
test_decrease_precise_ip_complicated	|| ret=$? ; count_result $ret ; ret=0

cleanup

if [ ${err_cnt} -gt 0 ] ; then
	exit 1
fi

if [ ${ok_cnt} -gt 0 ] ; then
	exit 0
fi

# Skip
exit 2
