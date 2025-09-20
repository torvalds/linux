#!/bin/bash
# perf ftrace tests
# SPDX-License-Identifier: GPL-2.0

set -e

# perf ftrace commands only works for root
if [ "$(id -u)" != 0 ]; then
    echo "perf ftrace test  [Skipped: no permission]"
    exit 2
fi

output=$(mktemp /tmp/__perf_test.ftrace.XXXXXX)

cleanup() {
  rm -f "${output}"

  trap - EXIT TERM INT
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

# this will be set in test_ftrace_trace()
target_function=

test_ftrace_list() {
    echo "perf ftrace list test"
    perf ftrace -F > "${output}"
    # this will be used in test_ftrace_trace()
    sleep_functions=$(grep 'sys_.*sleep$' "${output}")
    echo "syscalls for sleep:"
    echo "${sleep_functions}"
    echo "perf ftrace list test  [Success]"
}

test_ftrace_trace() {
    echo "perf ftrace trace test"
    perf ftrace trace --graph-opts depth=5 sleep 0.1 > "${output}"
    # it should have some function name contains 'sleep'
    grep "^#" "${output}"
    grep -F 'sleep()' "${output}"
    # find actual syscall function name
    for FN in ${sleep_functions}; do
	if grep -q "${FN}" "${output}"; then
	    target_function="${FN}"
	    echo "perf ftrace trace test  [Success]"
	    return
	fi
    done

    echo "perf ftrace trace test  [Failure: sleep syscall not found]"
    exit 1
}

test_ftrace_latency() {
    echo "perf ftrace latency test"
    echo "target function: ${target_function}"
    perf ftrace latency -T "${target_function}" sleep 0.1 > "${output}"
    grep "^#" "${output}"
    grep "###" "${output}"
    echo "perf ftrace latency test  [Success]"
}

test_ftrace_profile() {
    echo "perf ftrace profile test"
    perf ftrace profile --graph-opts depth=5 sleep 0.1 > "${output}"
    grep ^# "${output}"
    time_re="[[:space:]]+1[[:digit:]]{5}\.[[:digit:]]{3}"
    # 100283.000 100283.000 100283.000          1   __x64_sys_clock_nanosleep
    # Check for one *clock_nanosleep line with a Count of just 1 that takes a bit more than 0.1 seconds
    # Strip the _x64_sys part to work with other architectures
    grep -E "^${time_re}${time_re}${time_re}[[:space:]]+1[[:space:]]+.*clock_nanosleep" "${output}"
    echo "perf ftrace profile test  [Success]"
}

test_ftrace_list
test_ftrace_trace
test_ftrace_latency
test_ftrace_profile

cleanup
exit 0
