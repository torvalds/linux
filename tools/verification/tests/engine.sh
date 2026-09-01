#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
test_begin() {
	# Count tests to allow the test harness to double-check if all were
	# included correctly.
	ctr=0
	[ -z "$RV" ] && RV="../rv/rv"
	[ -z "$RVGEN" ] && RVGEN="python3 ../rvgen"
	[ -z "$GOLDEN_DIR" ] && GOLDEN_DIR="tests/golden"
	[ -n "$TEST_COUNT" ] && echo "1..$TEST_COUNT"
}

failure() {
	fail=1
	if [ $# -gt 0 ]; then
		failbuf+="$1"
		failbuf+=$'\n'
	fi
}

report() {
	local desc="$1"

	if [ "$fail" -eq 0 ]; then
		echo "ok $ctr - $desc"
	else
		# Add output and exit code as comments in case of failure
		echo "not ok $ctr - $desc"
		echo -n "$failbuf"
		echo "$result" | col -b | while read -r line; do echo "# $line"; done
		printf "#\n# exit code %s\n" "$exitcode"
	fi
}

_check() {
	local command=$2
	local expected_exitcode=${3:-0}
	local expected_output=$4
	local unexpected_output=$5
	local all_lines_pattern=$6
	local patterns="$expected_output $unexpected_output $all_lines_pattern"
	local bgpid pid

	eval "$TIMEOUT" "$command" &> check_output.$$ &
	bgpid=$!

	if grep -q "\$pid" <<< "$patterns"; then
		for _ in {1..30}; do
			pid=$(pgrep -f "${command%%[|;&>]*}" | tail -n1)
			[ -n "$pid" ] && break
			sleep 0.1
		done
	fi

	wait $bgpid
	exitcode=$?
	result=$(tr -d '\0' < check_output.$$)
	rm -f check_output.$$

	failbuf=''
	fail=0

	# Suppress any other error if a needed pid is empty
	if [ -z "$pid" ] && grep -q "\$pid" <<< "$patterns"; then
		result=''
		failure "# Empty pid for $command"
		return 1
	fi

	expected_output="${expected_output//\$pid/$pid}"
	unexpected_output="${unexpected_output//\$pid/$pid}"
	all_lines_pattern="${all_lines_pattern//\$pid/$pid}"

	# Test if the results matches if requested
	if [ -n "$expected_output" ] && ! grep -qe "$expected_output" <<< "$result"; then
		failure "# Output match failed: \"$expected_output\""
	fi

	if [ -n "$unexpected_output" ] && grep -qe "$unexpected_output" <<< "$result"; then
		failure "# Output non-match failed: \"$unexpected_output\""
	fi

	if [ -n "$all_lines_pattern" ] && grep -vqe "$all_lines_pattern" <<< "$result"; then
		failure "# All-lines pattern failed: \"$all_lines_pattern\""
	fi

	if [ $exitcode -ne "$expected_exitcode" ]; then
		failure "# Expected exit code $expected_exitcode"
	fi
}

check() {
	# Simple check: run the command with given arguments and test exit code.
	# If TEST_COUNT is set, run the test. Otherwise, just count.
	ctr=$((ctr + 1))
	if [ -n "$TEST_COUNT" ]; then
		_check "$@"
		report "$1"
	fi
}

check_if_exists() {
	# Conditional check that skips if a file or folder doesn't exist
	local desc=$1
	local command=$2
	local file=$3
	local expected_output=$4
	local unexpected_output=$5
	local all_lines_pattern=$6

	ctr=$((ctr + 1))
	if [ -n "$TEST_COUNT" ]; then
		if [ ! -e "$file" ]; then
			echo "ok $ctr - $desc # SKIP file not found: $file"
		else
			_check "$desc" "$command" 0 "$expected_output" \
				"$unexpected_output" "$all_lines_pattern"
			report "$desc"
		fi
	fi
}

check_and_compare_folder() {
	# Run command, compare generated folder to golden, and cleanup
	local desc=$1
	local command=$2
	local generated_dir=$3
	local expected_output=$4
	local unexpected_output=$5
	local golden_dir="$GOLDEN_DIR/$generated_dir"

	ctr=$((ctr + 1))
	if [ -n "$TEST_COUNT" ]; then
		rm -rf "$generated_dir"
		_check "$desc" "$command" 0 "$expected_output" "$unexpected_output"

		if [ "$fail" -eq 0 ] && [ ! -d "$generated_dir" ]; then
			failure "# Generated directory not found: $generated_dir"
		fi

		if [ "$fail" -ne 0 ]; then
			:
		elif ! diff -r "$generated_dir" "$golden_dir" &> /dev/null; then
			failure "# Directories differ:"
			failbuf+=$(diff -r "$generated_dir" "$golden_dir" 2>&1 | sed 's/^/#   /')
			failbuf+=$'\n'
		fi

		report "$1"

		rm -rf "$generated_dir"
	fi
}

set_timeout() {
	TIMEOUT="timeout -v -k 30s $1"
}

set_expected_timeout() {
	TIMEOUT="timeout --preserve-status -k 30s $1"
}

unset_timeout() {
	unset TIMEOUT
}

test_end() {
	# If running without TEST_COUNT, tests are not actually run, just
	# counted. In that case, re-run the test with the correct count.
	[ -z "$TEST_COUNT" ] && TEST_COUNT=$ctr exec bash "$0" || true
}

# Avoid any environmental discrepancies
export LC_ALL=C
unset_timeout
