#!/bin/sh
# Check branch stack sampling

# SPDX-License-Identifier: GPL-2.0
# German Gomez <german.gomez@arm.com>, 2022

# we need a C compiler to build the test programs
# so bail if none is found
if ! [ -x "$(command -v cc)" ]; then
	echo "failed: no compiler, install gcc"
	exit 2
fi

# skip the test if the hardware doesn't support branch stack sampling
# and if the architecture doesn't support filter types: any,save_type,u
if ! perf record -o- --no-buildid --branch-filter any,save_type,u -- true > /dev/null 2>&1 ; then
	echo "skip: system doesn't support filter types: any,save_type,u"
	exit 2
fi

TMPDIR=$(mktemp -d /tmp/__perf_test.program.XXXXX)

cleanup() {
	rm -rf $TMPDIR
}

trap cleanup exit term int

gen_test_program() {
	# generate test program
	cat << EOF > $1
#define BENCH_RUNS 999999
int cnt;
void bar(void) {
}			/* return */
void foo(void) {
	bar();		/* call */
}			/* return */
void bench(void) {
  void (*foo_ind)(void) = foo;
  if ((cnt++) % 3)	/* branch (cond) */
    foo();		/* call */
  bar();		/* call */
  foo_ind();		/* call (ind) */
}
int main(void)
{
  int cnt = 0;
  while (1) {
    if ((cnt++) > BENCH_RUNS)
      break;
    bench();		/* call */
  }			/* branch (uncond) */
  return 0;
}
EOF
}

test_user_branches() {
	echo "Testing user branch stack sampling"

	gen_test_program "$TEMPDIR/program.c"
	cc -fno-inline -g "$TEMPDIR/program.c" -o $TMPDIR/a.out

	perf record -o $TMPDIR/perf.data --branch-filter any,save_type,u -- $TMPDIR/a.out > /dev/null 2>&1
	perf script -i $TMPDIR/perf.data --fields brstacksym | xargs -n1 > $TMPDIR/perf.script

	# example of branch entries:
	# 	foo+0x14/bar+0x40/P/-/-/0/CALL

	set -x
	egrep -m1 "^bench\+[^ ]*/foo\+[^ ]*/IND_CALL$"	$TMPDIR/perf.script
	egrep -m1 "^foo\+[^ ]*/bar\+[^ ]*/CALL$"	$TMPDIR/perf.script
	egrep -m1 "^bench\+[^ ]*/foo\+[^ ]*/CALL$"	$TMPDIR/perf.script
	egrep -m1 "^bench\+[^ ]*/bar\+[^ ]*/CALL$"	$TMPDIR/perf.script
	egrep -m1 "^bar\+[^ ]*/foo\+[^ ]*/RET$"		$TMPDIR/perf.script
	egrep -m1 "^foo\+[^ ]*/bench\+[^ ]*/RET$"	$TMPDIR/perf.script
	egrep -m1 "^bench\+[^ ]*/bench\+[^ ]*/COND$"	$TMPDIR/perf.script
	egrep -m1 "^main\+[^ ]*/main\+[^ ]*/UNCOND$"	$TMPDIR/perf.script
	set +x

	# some branch types are still not being tested:
	# IND COND_CALL COND_RET SYSCALL SYSRET IRQ SERROR NO_TX
}

# first argument <arg0> is the argument passed to "--branch-stack <arg0>,save_type,u"
# second argument are the expected branch types for the given filter
test_filter() {
	local filter=$1
	local expect=$2

	echo "Testing branch stack filtering permutation ($filter,$expect)"

	gen_test_program "$TEMPDIR/program.c"
	cc -fno-inline -g "$TEMPDIR/program.c" -o $TMPDIR/a.out

	perf record -o $TMPDIR/perf.data --branch-filter $filter,save_type,u -- $TMPDIR/a.out > /dev/null 2>&1
	perf script -i $TMPDIR/perf.data --fields brstack | xargs -n1 > $TMPDIR/perf.script

	# fail if we find any branch type that doesn't match any of the expected ones
	# also consider UNKNOWN branch types (-)
	if egrep -vm1 "^[^ ]*/($expect|-|( *))$" $TMPDIR/perf.script; then
		return 1
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
