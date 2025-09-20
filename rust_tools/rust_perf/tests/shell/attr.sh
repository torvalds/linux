#!/bin/bash
# Perf attribute expectations test
# SPDX-License-Identifier: GPL-2.0

err=0

cleanup() {
  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

shelldir=$(dirname "$0")
perf_path=$(which perf)
python "${shelldir}"/lib/attr.py -d "${shelldir}"/attr -v -p "$perf_path"
cleanup
exit $err
