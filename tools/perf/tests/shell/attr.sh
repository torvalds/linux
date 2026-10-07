#!/bin/bash
# Perf attribute expectations test
# SPDX-License-Identifier: GPL-2.0

err=0

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

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
$PYTHON "${shelldir}"/lib/attr.py -d "${shelldir}"/attr -v -p "$perf_path"
err=$?
cleanup
exit $err
