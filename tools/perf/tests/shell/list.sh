#!/bin/sh
# perf list tests
# SPDX-License-Identifier: GPL-2.0

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

list_output=$(mktemp /tmp/__perf_test.list_output.json.XXXXX)

cleanup() {
  rm -f "${list_output}"

  trap - EXIT TERM INT
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

test_list_json() {
  echo "Json output test"
  perf list -j -o "${list_output}"
  $PYTHON -m json.tool "${list_output}"
  echo "Json output test [Success]"
}

test_list_json
cleanup
exit 0
