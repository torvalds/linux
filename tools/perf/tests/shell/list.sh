#!/bin/sh
# perf list tests
# SPDX-License-Identifier: GPL-2.0

set -e
err=0

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

test_list_json() {
  echo "Json output test"
  perf list -j | $PYTHON -m json.tool
  echo "Json output test [Success]"
}

test_list_json
exit $err
