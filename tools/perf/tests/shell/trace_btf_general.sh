#!/bin/bash
# perf trace BTF general tests
# SPDX-License-Identifier: GPL-2.0

err=0

# shellcheck source=lib/probe.sh
. "$(dirname $0)"/lib/probe.sh

file1=$(mktemp /tmp/file1_XXXX)
file2=$(echo $file1 | sed 's/file1/file2/g')

buffer="buffer content"
perf_config_tmp=$(mktemp /tmp/.perfconfig_XXXXX)

trap cleanup EXIT TERM INT HUP

check_vmlinux() {
  echo "Checking if vmlinux BTF exists"
  if [ ! -f /sys/kernel/btf/vmlinux ]
  then
    echo "Skipped due to missing vmlinux BTF"
    return 2
  fi
  return 0
}

trace_test_string() {
  echo "Testing perf trace's string augmentation"
  output="$(perf trace --sort-events -e renameat* --max-events=1 -- mv ${file1} ${file2} 2>&1)"
  if ! echo "$output" | grep -q -E "^mv/[0-9]+ renameat(2)?\(.*, \"${file1}\", .*, \"${file2}\", .*\) += +[0-9]+$"
  then
    printf "String augmentation test failed, output:\n$output\n"
    err=1
  fi
}

trace_test_buffer() {
  echo "Testing perf trace's buffer augmentation"
  # echo will insert a newline (\10) at the end of the buffer
  output="$(perf trace --sort-events -e write --max-events=1 -- echo "${buffer}" 2>&1)"
  if ! echo "$output" | grep -q -E "^echo/[0-9]+ write\([0-9]+, ${buffer}.*, [0-9]+\) += +[0-9]+$"
  then
    printf "Buffer augmentation test failed, output:\n$output\n"
    err=1
  fi
}

trace_test_struct_btf() {
  echo "Testing perf trace's struct augmentation"
  output="$(perf trace --sort-events -e clock_nanosleep --force-btf --max-events=1 -- sleep 1 2>&1)"
  if ! echo "$output" | grep -q -E "^sleep/[0-9]+ clock_nanosleep\(0, 0, \{1,.*\}, 0x[0-9a-f]+\) += +[0-9]+$"
  then
	printf "BTF struct augmentation test failed, output:\n$output\n"
    err=1
  fi
}

cleanup() {
  rm -rf ${file1} ${file2} ${perf_config_tmp}
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}

# don't overwrite user's perf config
trace_config() {
  export PERF_CONFIG=${perf_config_tmp}
  perf config trace.show_arg_names=false trace.show_duration=false \
    trace.show_timestamp=false trace.args_alignment=0
}

skip_if_no_perf_trace || exit 2
check_vmlinux || exit 2
[ "$(id -u)" = 0 ] || exit 2

trace_config

trace_test_string

if [ $err = 0 ]; then
  trace_test_buffer
fi

if [ $err = 0 ]; then
  trace_test_struct_btf
fi

cleanup

exit $err
