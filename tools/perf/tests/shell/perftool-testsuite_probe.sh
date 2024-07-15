#!/bin/bash
# perftool-testsuite_probe
# SPDX-License-Identifier: GPL-2.0

test -d "$(dirname "$0")/base_probe" || exit 2
cd "$(dirname "$0")/base_probe" || exit 2
status=0

PERFSUITE_RUN_DIR=$(mktemp -d /tmp/"$(basename "$0" .sh)".XXX)
export PERFSUITE_RUN_DIR

for testcase in setup.sh test_*; do                  # skip setup.sh if not present or not executable
     test -x "$testcase" || continue
     ./"$testcase"
     (( status += $? ))
done

if ! [ "$PERFTEST_KEEP_LOGS" = "y" ]; then
	rm -rf "$PERFSUITE_RUN_DIR"
fi

test $status -ne 0 && exit 1
exit 0
