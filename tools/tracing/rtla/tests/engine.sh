#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
test_begin() {
	# Count tests to allow the test harness to double-check if all were
	# included correctly.
	ctr=0
	[ -z "$RTLA" ] && RTLA="./rtla"
	[ -n "$TEST_COUNT" ] && echo "1..$TEST_COUNT"
}

reset_osnoise() {
	# Reset osnoise options to default and remove any dangling instances created
	# by improperly exited rtla runs.
	pushd /sys/kernel/tracing >/dev/null || return 1

	# Remove dangling instances created by previous rtla run
	echo 0 > tracing_thresh
	cd instances
	for i in osnoise_top osnoise_hist timerlat_top timerlat_hist
	do
		[ ! -d "$i" ] && continue
		rmdir "$i"
	done

	# Reset options to default
	# Note: those are copied from the default values of osnoise_data
	# in kernel/trace/trace_osnoise.c
	cd ../osnoise
	echo all > cpus
	echo DEFAULTS > options
	echo 1000000 > period_us
	echo 0 > print_stack
	echo 1000000 > runtime_us
	echo 0 > stop_tracing_total_us
	echo 0 > stop_tracing_us
	echo 1000 > timerlat_period_us

	popd >/dev/null
}

check() {
	test_name=$0
	tested_command=$1
	expected_exitcode=${3:-0}
	expected_output=$4
	unexpected_output=$5
	# Simple check: run rtla with given arguments and test exit code.
	# If TEST_COUNT is set, run the test. Otherwise, just count.
	ctr=$(($ctr + 1))
	if [ -n "$TEST_COUNT" ]
	then
		# Reset osnoise options before running test.
		[ "$NO_RESET_OSNOISE" == 1 ] || reset_osnoise
		# Run rtla; in case of failure, include its output as comment
		# in the test results.
		result=$(eval stdbuf -oL $TIMEOUT "$RTLA" $2 2>&1); exitcode=$?
		failbuf=''
		fail=0

		# Test if the results matches if requested
		if [ -n "$expected_output" ] && ! grep -qE "$expected_output" <<< "$result"
		then
			fail=1
			failbuf+=$(printf "# Output match failed: \"%s\"" "$expected_output")
			failbuf+=$'\n'
		fi

		if [ -n "$unexpected_output" ] && grep -qE "$unexpected_output" <<< "$result"
		then
			fail=1
			failbuf+=$(printf "# Output non-match failed: \"%s\"" "$unexpected_output")
			failbuf+=$'\n'
		fi

		if [ $exitcode -eq $expected_exitcode ] && [ $fail -eq 0 ]
		then
			echo "ok $ctr - $1"
		else
			# Add rtla output and exit code as comments in case of failure
			echo "not ok $ctr - $1"
			echo -n "$failbuf"
			echo "$result" | col -b | while read line; do echo "# $line"; done
			printf "#\n# exit code %s\n" $exitcode
		fi
	fi
}

check_with_osnoise_options() {
	# Do the same as "check", but with pre-set osnoise options.
	# Note: rtla should reset the osnoise options, this is used to test
	# if it indeed does so.
	# Save original arguments
	arg1=$1
	arg2=$2
	arg3=$3

	# Apply osnoise options (if not dry run)
	if [ -n "$TEST_COUNT" ]
	then
		[ "$NO_RESET_OSNOISE" == 1 ] || reset_osnoise
		shift
		shift
		while shift
		do
			[ "$1" == "" ] && continue
			option=$(echo $1 | cut -d '=' -f 1)
			value=$(echo $1 | cut -d '=' -f 2)
			echo "option: $option, value: $value"
			echo "$value" > "/sys/kernel/tracing/osnoise/$option" || return 1
		done
	fi

	NO_RESET_OSNOISE=1 check "$arg1" "$arg2" "$arg3"
}

set_timeout() {
	TIMEOUT="timeout -v -k 15s $1"
}

unset_timeout() {
	unset TIMEOUT
}

set_no_reset_osnoise() {
	NO_RESET_OSNOISE=1
}

unset_no_reset_osnoise() {
	unset NO_RESET_OSNOISE
}

test_end() {
	# If running without TEST_COUNT, tests are not actually run, just
	# counted. In that case, re-run the test with the correct count.
	[ -z "$TEST_COUNT" ] && TEST_COUNT=$ctr exec bash $0 || true
}

# Avoid any environmental discrepancies
export LC_ALL=C
unset_timeout
