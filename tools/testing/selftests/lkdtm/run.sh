#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# This reads tests.txt for the list of LKDTM tests to invoke. Any marked
# with a leading "#" are skipped. The rest of the line after the
# test name is either the text to look for in dmesg for a "success",
# or the rationale for why a test is marked to be skipped.
#
set -e
TRIGGER=/sys/kernel/debug/provoke-crash/DIRECT
CLEAR_ONCE=/sys/kernel/debug/clear_warn_once
KSELFTEST_SKIP_TEST=4

# Verify we have LKDTM available in the kernel.
if [ ! -r $TRIGGER ] ; then
	/sbin/modprobe -q lkdtm || true
	if [ ! -r $TRIGGER ] ; then
		echo "Cannot find $TRIGGER (missing CONFIG_LKDTM?)"
	else
		echo "Cannot write $TRIGGER (need to run as root?)"
	fi
	# Skip this test
	exit $KSELFTEST_SKIP_TEST
fi

# Figure out which test to run from our script name.
test=$(basename $0 .sh)
# Look up details about the test from master list of LKDTM tests.
line=$(grep -E '^#?'"$test"'\b' tests.txt)
if [ -z "$line" ]; then
	echo "Skipped: missing test '$test' in tests.txt"
	exit $KSELFTEST_SKIP_TEST
fi
# Check that the test is known to LKDTM.
if ! grep -E -q '^'"$test"'$' "$TRIGGER" ; then
	echo "Skipped: test '$test' missing in $TRIGGER!"
	exit $KSELFTEST_SKIP_TEST
fi

# Extract notes/expected output from test list.
test=$(echo "$line" | cut -d" " -f1)
if echo "$line" | grep -q ' ' ; then
	expect=$(echo "$line" | cut -d" " -f2-)
else
	expect=""
fi

# If the test is commented out, report a skip
if echo "$test" | grep -q '^#' ; then
	test=$(echo "$test" | cut -c2-)
	if [ -z "$expect" ]; then
		expect="crashes entire system"
	fi
	echo "Skipping $test: $expect"
	exit $KSELFTEST_SKIP_TEST
fi

# If no expected output given, assume an Oops with back trace is success.
if [ -z "$expect" ]; then
	expect="call trace:"
fi

# Prepare log for report checking
LOG=$(mktemp --tmpdir -t lkdtm-log-XXXXXX)
DMESG=$(mktemp --tmpdir -t lkdtm-dmesg-XXXXXX)
cleanup() {
	rm -f "$LOG" "$DMESG"
}
trap cleanup EXIT

# Reset WARN_ONCE counters so we trip it each time this runs.
if [ -w $CLEAR_ONCE ] ; then
	echo 1 > $CLEAR_ONCE
fi

# Save existing dmesg so we can detect new content below
dmesg > "$DMESG"

# Most shells yell about signals and we're expecting the "cat" process
# to usually be killed by the kernel. So we have to run it in a sub-shell
# and silence errors.
($SHELL -c 'cat <(echo '"$test"') >'"$TRIGGER" 2>/dev/null) || true

# Record and dump the results
dmesg | comm --nocheck-order -13 "$DMESG" - > "$LOG" || true

cat "$LOG"
# Check for expected output
if grep -E -qi "$expect" "$LOG" ; then
	echo "$test: saw '$expect': ok"
	exit 0
else
	if grep -E -qi XFAIL: "$LOG" ; then
		echo "$test: saw 'XFAIL': [SKIP]"
		exit $KSELFTEST_SKIP_TEST
	else
		echo "$test: missing '$expect': [FAIL]"
		exit 1
	fi
fi
