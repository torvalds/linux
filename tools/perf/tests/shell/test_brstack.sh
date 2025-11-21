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

is_arm64() {
	[ "$(uname -m)" = "aarch64" ];
}

check_branches() {
	if ! tr -s ' ' '\n' < "$TMPDIR/perf.script" | grep -E -m1 -q "$1"; then
		echo "Branches missing $1"
		err=1
	fi
}

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
		check_branches "$x"
	done

	# Dump addresses only this time
	perf script -i "$TMPDIR/perf.data" --fields brstack | \
		tr ' ' '\n' > "$TMPDIR/perf.script"

	# There should be no kernel addresses with the u option, in either
	# source or target addresses.
	if grep -E -m1 "0x[89a-f][0-9a-f]{15}" $TMPDIR/perf.script; then
		echo "ERROR: Kernel address found in user mode"
		err=1
	fi
	# some branch types are still not being tested:
	# IND COND_CALL COND_RET SYSRET SERROR NO_TX
}

test_trap_eret_branches() {
	echo "Testing trap & eret branches"
	if ! is_arm64; then
		echo "skip: not arm64"
	else
		perf record -o $TMPDIR/perf.data --branch-filter any,save_type,u,k -- \
			perf test -w traploop 1000
		perf script -i $TMPDIR/perf.data --fields brstacksym | \
			tr ' ' '\n' > $TMPDIR/perf.script

		# BRBINF<n>.TYPE == TRAP are mapped to PERF_BR_IRQ by the BRBE driver
		check_branches "^trap_bench\+[^ ]+/[^ ]/IRQ/"
		check_branches "^[^ ]+/trap_bench\+[^ ]+/ERET/"
	fi
}

test_kernel_branches() {
	echo "Testing that k option only includes kernel source addresses"

	if ! perf record --branch-filter any,k -o- -- true > /dev/null; then
		echo "skip: not enough privileges"
	else
		perf record -o $TMPDIR/perf.data --branch-filter any,k -- \
			perf bench syscall basic --loop 1000
		perf script -i $TMPDIR/perf.data --fields brstack | \
			tr ' ' '\n' > $TMPDIR/perf.script

		# Example of branch entries:
		#       "0xffffffff93bda241/0xffffffff93bda20f/M/-/-/..."
		# Source addresses come first and target address can be either
		# userspace or kernel even with k option, as long as the source
		# is in kernel.

		#Look for source addresses with top bit set
		if ! grep -E -m1 "^0x[89a-f][0-9a-f]{15}" $TMPDIR/perf.script; then
			echo "ERROR: Kernel branches missing"
			err=1
		fi
		# Look for no source addresses without top bit set
		if grep -E -m1 "^0x[0-7][0-9a-f]{0,15}" $TMPDIR/perf.script; then
			echo "ERROR: User branches found with kernel filter"
			err=1
		fi
	fi
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

test_syscall() {
	echo "Testing syscalls"
	# skip if perf doesn't have enough privileges
	if ! perf record --branch-filter any,k -o- -- true > /dev/null; then
		echo "skip: not enough privileges"
	else
		perf record -o $TMPDIR/perf.data --branch-filter \
			any_call,save_type,u,k -c 10000 -- \
			perf bench syscall basic --loop 1000
		perf script -i $TMPDIR/perf.data --fields brstacksym | \
			tr ' ' '\n' > $TMPDIR/perf.script

		check_branches "getppid[^ ]*/SYSCALL/"
	fi
}
set -e

test_user_branches
test_syscall
test_kernel_branches
test_trap_eret_branches

any_call="CALL|IND_CALL|COND_CALL|SYSCALL|IRQ"

if is_arm64; then
	any_call="$any_call|FAULT_DATA|FAULT_INST"
fi

test_filter "any_call" "$any_call"
test_filter "call"	"CALL|SYSCALL"
test_filter "cond"	"COND"
test_filter "any_ret"	"RET|COND_RET|SYSRET|ERET"

test_filter "call,cond"		"CALL|SYSCALL|COND"
test_filter "any_call,cond"	    "$any_call|COND"
test_filter "any_call,cond,any_ret" "$any_call|COND|RET|COND_RET"

cleanup
exit $err
