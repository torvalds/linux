#!/bin/bash
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
  # Generate perf list json output into list_output file.
  perf list -j -o "${list_output}"
  # Validate the json using python, redirect the json copy to /dev/null as
  # otherwise the test may block writing to stdout.
  $PYTHON -m json.tool "${list_output}" /dev/null
  echo "Json output test [Success]"
}

test_list_json
cleanup
exit 0
