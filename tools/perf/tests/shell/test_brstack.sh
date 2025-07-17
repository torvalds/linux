#!/bin/bash
# Check branch stack sampling

# SPDX-License-Identifier: GPL-2.0
# German Gomez <german.gomez@arm.com>, 2022

shelldir=$(dirname "$0")
# shellcheck source=lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

# skip the test if the hardware doesn't support branch stack sampling
# and if the architecture doesn't support filter types: any,save_type,u
if ! perf record -o- --no-buildid --branch-filter any,save_type,u -- true > /dev/null 2>&1 ; then
	echo "skip: system doesn't support filter types: any,save_type,u"
	exit 2
fi

skip_test_missing_symbol brstack_bench

err=0
TMPDIR=$(mktemp -d /tmp/__perf_test.program.XXXXX)
TESTPROG="perf test -w brstack"

cleanup() {
	rm -rf $TMPDIR
	trap - EXIT TERM INT
}

trap_cleanup() {
	set +e
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

test_user_branches() {
	echo "Testing user branch stack sampling"

	perf record -o "$TMPDIR/perf.data" --branch-filter any,save_type,u -- ${TESTPROG} > "$TMPDIR/record.txt" 2>&1
	perf script -i "$TMPDIR/perf.data" --fields brstacksym > "$TMPDIR/perf.script"

	# example of branch entries:
	# 	brstack_foo+0x14/brstack_bar+0x40/P/-/-/0/CALL

	expected=(
		"^brstack_bench\+[^ ]*/brstack_foo\+[^ ]*/IND_CALL/.*$"
		"^brstack_foo\+[^ ]*/brstack_bar\+[^ ]*/CALL/.*$"
		"^brstack_bench\+[^ ]*/brstack_foo\+[^ ]*/CALL/.*$"
		"^brstack_bench\+[^ ]*/brstack_bar\+[^ ]*/CALL/.*$"
		"^brstack_bar\+[^ ]*/brstack_foo\+[^ ]*/RET/.*$"
		"^brstack_foo\+[^ ]*/brstack_bench\+[^ ]*/RET/.*$"
		"^brstack_bench\+[^ ]*/brstack_bench\+[^ ]*/COND/.*$"
		"^brstack\+[^ ]*/brstack\+[^ ]*/UNCOND/.*$"
	)
	for x in "${expected[@]}"
	do
		if ! tr -s ' ' '\n' < "$TMPDIR/perf.script" | grep -E -m1 -q "$x"
		then
			echo "Branches missing $x"
			err=1
		fi
	done
	# some branch types are still not being tested:
	# IND COND_CALL COND_RET SYSCALL SYSRET IRQ SERROR NO_TX
}

# first argument <arg0> is the argument passed to "--branch-stack <arg0>,save_type,u"
# second argument are the expected branch types for the given filter
test_filter() {
	test_filter_filter=$1
	test_filter_expect=$2

	echo "Testing branch stack filtering permutation ($test_filter_filter,$test_filter_expect)"
	perf record -o "$TMPDIR/perf.data" --branch-filter "$test_filter_filter,save_type,u" -- ${TESTPROG}  > "$TMPDIR/record.txt" 2>&1
	perf script -i "$TMPDIR/perf.data" --fields brstack > "$TMPDIR/perf.script"

	# fail if we find any branch type that doesn't match any of the expected ones
	# also consider UNKNOWN branch types (-)
	if [ ! -s "$TMPDIR/perf.script" ]
	then
		echo "Empty script output"
		err=1
		return
	fi
	# Look for lines not matching test_filter_expect ignoring issues caused
	# by empty output
	tr -s ' ' '\n' < "$TMPDIR/perf.script" | grep '.' | \
	  grep -E -vm1 "^[^ ]*/($test_filter_expect|-|( *))/.*$" \
	  > "$TMPDIR/perf.script-filtered" || true
	if [ -s "$TMPDIR/perf.script-filtered" ]
	then
		echo "Unexpected branch filter in script output"
		cat "$TMPDIR/perf.script"
		err=1
		return
	fi
}

set -e

test_user_branches

test_filter "any_call"	"CALL|IND_CALL|COND_CALL|SYSCALL|IRQ"
test_filter "call"	"CALL|SYSCALL"
test_filter "cond"	"COND"
test_filter "any_ret"	"RET|COND_RET|SYSRET|ERET"

test_filter "call,cond"		"CALL|SYSCALL|COND"
test_filter "any_call,cond"		"CALL|IND_CALL|COND_CALL|IRQ|SYSCALL|COND"
test_filter "cond,any_call,any_ret"	"COND|CALL|IND_CALL|COND_CALL|SYSCALL|IRQ|RET|COND_RET|SYSRET|ERET"

cleanup
exit $err
