#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#

set -e

DRIVER="./page_pool/bench_page_pool.ko"
result=""

function run_test()
{
	rmmod "bench_page_pool.ko" || true
	insmod $DRIVER > /dev/null 2>&1
	result=$(dmesg | tail -10)
	echo "$result"

	echo
	echo "Fast path results:"
	echo "${result}" | grep -o -E "no-softirq-page_pool01 Per elem: ([0-9]+) cycles\(tsc\) ([0-9]+\.[0-9]+) ns"

	echo
	echo "ptr_ring results:"
	echo "${result}" | grep -o -E "no-softirq-page_pool02 Per elem: ([0-9]+) cycles\(tsc\) ([0-9]+\.[0-9]+) ns"

	echo
	echo "slow path results:"
	echo "${result}" | grep -o -E "no-softirq-page_pool03 Per elem: ([0-9]+) cycles\(tsc\) ([0-9]+\.[0-9]+) ns"
}

run_test

exit 0
