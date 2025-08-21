#!/bin/bash
# perf trace enum augmentation tests
# SPDX-License-Identifier: GPL-2.0

err=0

syscall="landlock_add_rule"
non_syscall="timer:hrtimer_start"

TESTPROG="perf test -w landlock"

# shellcheck source=lib/probe.sh
. "$(dirname $0)"/lib/probe.sh
skip_if_no_perf_trace || exit 2
[ "$(id -u)" = 0 ] || exit 2

check_vmlinux() {
  echo "Checking if vmlinux exists"
  if [ ! -f /sys/kernel/btf/vmlinux ]
  then
    echo "trace+enum test [Skipped missing vmlinux BTF support]"
    err=2
  fi
}

check_permissions() {
  if perf trace -e $syscall $TESTPROG 2>&1 | grep -q "Operation not permitted"
  then
    echo "trace+enum test [Skipped permissions]"
    err=2
  fi
}

trace_landlock() {
  echo "Tracing syscall ${syscall}"

  # test flight just to see if landlock_add_rule is available
  if ! perf trace $TESTPROG 2>&1 | grep -q landlock
  then
    echo "No landlock system call found, skipping to non-syscall tracing."
    return
  fi

  output="$(perf trace -e $syscall $TESTPROG 2>&1)"
  if echo "$output" | grep -q -E ".*landlock_add_rule\(ruleset_fd: 11, rule_type: (LANDLOCK_RULE_PATH_BENEATH|LANDLOCK_RULE_NET_PORT), rule_attr: 0x[a-f0-9]+, flags: 45\) = -1.*"
  then
    err=0
  else
	printf "[syscall failure] Failed to trace syscall $syscall, output:\n$output\n"
    err=1
  fi
}

trace_non_syscall() {
  echo "Tracing non-syscall tracepoint ${non_syscall}"
  output="$(perf trace -e $non_syscall --max-events=1 2>&1)"
  if echo "$output" | grep -q -E '.*timer:hrtimer_.*\(.*mode: HRTIMER_MODE_.*\)$'
  then
    err=0
  else
	printf "[tracepoint failure] Failed to trace tracepoint $non_syscall, output:\n$output\n"
    err=1
  fi
}

check_vmlinux
if [ $err = 0 ]; then
  check_permissions
fi

if [ $err = 0 ]; then
  trace_landlock
fi

if [ $err = 0 ]; then
  trace_non_syscall
fi

exit $err
