#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Run installed kselftest tests.
#
BASE_DIR=$(realpath $(dirname $0))
cd $BASE_DIR
TESTS="$BASE_DIR"/kselftest-list.txt
if [ ! -r "$TESTS" ] ; then
	echo "$0: Could not find list of tests to run ($TESTS)" >&2
	exit 1
fi
available="$(cat "$TESTS")"

. ./kselftest/runner.sh
ROOT=$PWD

if [ "$1" = "--summary" ] ; then
	logfile="$BASE_DIR"/output.log
	cat /dev/null > $logfile
fi

collections=$(echo "$available" | cut -d: -f1 | uniq)
for collection in $collections ; do
	[ -w /dev/kmsg ] && echo "kselftest: Running tests in $collection" >> /dev/kmsg
	tests=$(echo "$available" | grep "^$collection:" | cut -d: -f2)
	(cd "$collection" && run_many $tests)
done
