#!/bin/bash
# perf header tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_test_header.perf.data.XXXXX)
script_output=$(mktemp /tmp/__perf_test_header.perf.data.XXXXX.script)

cleanup() {
  rm -f "${perfdata}"
  rm -f "${perfdata}".old
  rm -f "${script_output}"

  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

check_header_output() {
  declare -a fields=(
    "captured"
    "hostname"
    "os release"
    "arch"
    "cpuid"
    "nrcpus"
    "event"
    "cmdline"
    "perf version"
    "sibling (cores|dies|threads)"
    "sibling threads"
    "total memory"
  )
  for i in "${fields[@]}"
  do
    if ! grep -q -E "$i" "${script_output}"
    then
      echo "Failed to find expected $i in output"
      err=1
    fi
  done
}

test_file() {
  echo "Test perf header file"

  perf record -o "${perfdata}" -- perf test -w noploop
  perf report --header-only -I -i "${perfdata}" > "${script_output}"
  check_header_output

  echo "Test perf header file [Done]"
}

test_pipe() {
  echo "Test perf header pipe"

  perf record -o - -- perf test -w noploop | perf report --header-only -I -i - > "${script_output}"
  check_header_output

  echo "Test perf header pipe [Done]"
}

test_file
test_pipe

cleanup
exit $err
