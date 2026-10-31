#!/bin/bash
# perf trace kernel symbol beautifier tests
# SPDX-License-Identifier: GPL-2.0

err=0

# shellcheck source=lib/probe.sh
. "$(dirname "$0")"/lib/probe.sh
skip_if_no_perf_trace || exit 2
[ "$(id -u)" = 0 ] || exit 2

test_ksym_call_site() {
  echo "Testing perf trace kernel symbol beautifier (call_site)"
  output="$(perf trace -e kmem:kmalloc --max-events=1 -- true 2>&1)"
  if ! echo "$output" | grep -q -E "call_site: [a-zA-Z_][a-zA-Z0-9_]*" || echo "$output" | grep -q -E "call_site: 0x[0-9a-fA-F]+"
  then
    printf "Call site kernel symbol beautification failed, output:\n%s\n" "$output"
    err=1
  fi
}

test_ksym_function_ptr() {
  echo "Testing perf trace kernel symbol beautifier (function pointer)"
  output="$(perf trace -e timer:hrtimer_start --max-events=1 -- sleep 0.01 2>&1)"
  if ! echo "$output" | grep -q -E "function: [a-zA-Z_][a-zA-Z0-9_]*" || echo "$output" | grep -q -E "function: 0x[0-9a-fA-F]+"
  then
    printf "Function pointer kernel symbol beautification failed, output:\n%s\n" "$output"
    err=1
  fi
}

test_ksym_call_site

if [ $err = 0 ]; then
  test_ksym_function_ptr
fi

exit $err
