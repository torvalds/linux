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

has_kaslr_bug() {
	[ "$(uname -m)" != "aarch64" ];
}

check_branches() {
	if ! tr -s ' ' '\n' < "$TMPDIR/perf.script" | grep -E -m1 -q "$1"; then
		echo "ERROR: Branches missing $1"
		err=1
	fi
}

test_user_branches() {
	echo "Testing user branch stack sampling"

	start_err=$err
	err=0
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

	# There should be no kernel addresses in the target with the u option.
	local regex="0x[89a-f][0-9a-f]{15}"
	if has_kaslr_bug; then
		# If the system has a kaslr bug that may leak kernel addresses
		# in the source of something like an ERET/SYSRET. Make the regex
		# more specific and just check the target address is in user
		# code.
		regex="^0x[0-9a-f]{0,16}/0x[89a-f][0-9a-f]{15}/"
	fi
	if grep -q -E -m1 "$regex" $TMPDIR/perf.script; then
		echo "Testing user branch stack sampling [Failed kernel address found in user mode]"
		err=1
	fi
	# some branch types are still not being tested:
	# IND COND_CALL COND_RET SYSRET SERROR NO_TX
	if [ $err -eq 0 ]; then
		echo "Testing user branch stack sampling [Passed]"
		err=$start_err
	else
		echo "Testing user branch stack sampling [Failed]"
	fi
}

test_trap_eret_branches() {
	echo "Testing trap & eret branches"

	if ! is_arm64; then
		echo "Testing trap & eret branches [Skipped not arm64]"
		return
	fi
	start_err=$err
	err=0
	perf record -o $TMPDIR/perf.data --branch-filter any,save_type,u,k -- \
		perf test -w traploop 1000 > "$TMPDIR/record.txt" 2>&1
	perf script -i $TMPDIR/perf.data --fields brstacksym | \
		tr ' ' '\n' > $TMPDIR/perf.script

	# BRBINF<n>.TYPE == TRAP are mapped to PERF_BR_IRQ by the BRBE driver
	check_branches "^trap_bench\+[^ ]+/[^ ]/IRQ/"
	check_branches "^[^ ]+/trap_bench\+[^ ]+/ERET/"
	if [ $err -eq 0 ]; then
		echo "Testing trap & eret branches [Passed]"
		err=$start_err
	else
		echo "Testing trap & eret branches [Failed]"
	fi
}

test_kernel_branches() {
	echo "Testing kernel branch sampling"

	if ! perf record --branch-filter any,k -o- -- true > "$TMPDIR/record.txt" 2>&1; then
		echo "Testing that k option [Skipped not enough privileges]"
		return
	fi
	start_err=$err
	err=0
	perf record -o $TMPDIR/perf.data --branch-filter any,k -- \
		perf bench syscall basic --loop 1000 > "$TMPDIR/record.txt" 2>&1
	perf script -i $TMPDIR/perf.data --fields brstack | \
		tr ' ' '\n' > $TMPDIR/perf.script

	# Example of branch entries:
	#       "0xffffffff93bda241/0xffffffff93bda20f/M/-/-/..."
	# Source addresses come first in user or kernel code. Next is the target
	# address that must be in the kernel.

	# Look for source addresses with top bit set
	if ! grep -q -E -m1 "^0x[89a-f][0-9a-f]{15}" $TMPDIR/perf.script; then
		echo "Testing kernel branch sampling [Failed kernel branches missing]"
		err=1
	fi
	# Look for no target addresses without top bit set
	if grep -q -E -m1 "^0x[0-9a-f]{0,16}/0x[0-7][0-9a-f]{1,15}/" $TMPDIR/perf.script; then
		echo "Testing kernel branch sampling [Failed user branches found]"
		err=1
	fi
	if [ $err -eq 0 ]; then
		echo "Testing kernel branch sampling [Passed]"
		err=$start_err
	else
		echo "Testing kernel branch sampling [Failed]"
	fi
}

# first argument <arg0> is the argument passed to "--branch-stack <arg0>,save_type,u"
# second argument are the expected branch types for the given filter
test_filter() {
	test_filter_filter=$1
	test_filter_expect=$2

	echo "Testing branch stack filtering permutation ($test_filter_filter,$test_filter_expect)"
	perf record -o "$TMPDIR/perf.data" --branch-filter "$test_filter_filter,save_type,u" -- \
		${TESTPROG}  > "$TMPDIR/record.txt" 2>&1
	perf script -i "$TMPDIR/perf.data" --fields brstack > "$TMPDIR/perf.script"

	# fail if we find any branch type that doesn't match any of the expected ones
	# also consider UNKNOWN branch types (-)
	if [ ! -s "$TMPDIR/perf.script" ]
	then
		echo "Testing branch stack filtering [Failed empty script output]"
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
		echo "Testing branch stack filtering [Failed unexpected branch filter]"
		cat "$TMPDIR/perf.script"
		err=1
		return
	fi
	echo "Testing branch stack filtering [Passed]"
}

test_syscall() {
	echo "Testing syscalls"
	# skip if perf doesn't have enough privileges
	if ! perf record --branch-filter any,k -o- -- true > "$TMPDIR/record.txt" 2>&1; then
		echo "Testing syscalls [Skipped: not enough privileges]"
		return
	fi
	start_err=$err
	err=0
	perf record -o $TMPDIR/perf.data --branch-filter \
		any_call,save_type,u,k -c 10007 -- \
		perf bench syscall basic --loop 8000  > "$TMPDIR/record.txt" 2>&1
	perf script -i $TMPDIR/perf.data --fields brstacksym | \
		tr ' ' '\n' > $TMPDIR/perf.script

	check_branches "getppid[^ ]*/SYSCALL/"

	if [ $err -eq 0 ]; then
		echo "Testing syscalls [Passed]"
		err=$start_err
	else
		echo "Testing syscalls [Failed]"
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
