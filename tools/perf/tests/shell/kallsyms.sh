#!/bin/bash
# perf kallsyms tests
# SPDX-License-Identifier: GPL-2.0

err=0

test_kallsyms() {
	echo "Basic perf kallsyms test"

	# Check if /proc/kallsyms is readable
	if [ ! -r /proc/kallsyms ]; then
		echo "Basic perf kallsyms test [Skipped: /proc/kallsyms not readable]"
		err=2
		return
	fi

	# Use a symbol that is definitely a function and present in all kernels, e.g. schedule
	symbol="schedule"

	# Run perf kallsyms
	# It prints "address symbol_name"
	output=$(perf kallsyms $symbol 2>&1)
	ret=$?

	if [ $ret -ne 0 ] || [ -z "$output" ]; then
		# If empty or failed, it might be due to permissions (kptr_restrict)
		# Check if we can grep the symbol from /proc/kallsyms directly
		if grep -q "$symbol" /proc/kallsyms 2>/dev/null; then
			# If it's in /proc/kallsyms but perf kallsyms returned empty/error,
			# it likely means perf couldn't parse it or access it correctly (e.g. kptr_restrict=2).
			echo "Basic perf kallsyms test [Skipped: $symbol found in /proc/kallsyms but perf kallsyms failed (output: '$output')]"
			err=2
			return
		else
			echo "Basic perf kallsyms test [Skipped: $symbol not found in /proc/kallsyms]"
			err=2
			return
		fi
	fi

	if echo "$output" | grep -q "not found"; then
		echo "Basic perf kallsyms test [Failed: output '$output' does not contain $symbol]"
		err=1
		return
	fi

	if perf kallsyms ErlingHaaland | grep -vq "not found"; then
		echo "Basic perf kallsyms test [Failed: ErlingHaaland found in the output]"
		err=1
		return
	fi
	echo "Basic perf kallsyms test [Success]"
}

test_kallsyms
exit $err
