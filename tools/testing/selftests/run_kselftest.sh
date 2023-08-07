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
	available=""
else
	available="$(cat "$TESTS")"
fi

. ./kselftest/runner.sh
ROOT=$PWD

usage()
{
	cat <<EOF
Usage: $0 [OPTIONS]
  -s | --summary		Print summary with detailed log in output.log
  -t | --test COLLECTION:TEST	Run TEST from COLLECTION
  -c | --collection COLLECTION	Run all tests from COLLECTION
  -l | --list			List the available collection:test entries
  -d | --dry-run		Don't actually run any tests
  -h | --help			Show this usage info
  -o | --override-timeout	Number of seconds after which we timeout
EOF
	exit $1
}

COLLECTIONS=""
TESTS=""
dryrun=""
kselftest_override_timeout=""
while true; do
	case "$1" in
		-s | --summary)
			logfile="$BASE_DIR"/output.log
			cat /dev/null > $logfile
			shift ;;
		-t | --test)
			TESTS="$TESTS $2"
			shift 2 ;;
		-c | --collection)
			COLLECTIONS="$COLLECTIONS $2"
			shift 2 ;;
		-l | --list)
			echo "$available"
			exit 0 ;;
		-d | --dry-run)
			dryrun="echo"
			shift ;;
		-o | --override-timeout)
			kselftest_override_timeout="$2"
			shift 2 ;;
		-h | --help)
			usage 0 ;;
		"")
			break ;;
		*)
			usage 1 ;;
	esac
done

# Add all selected collections to the explicit test list.
if [ -n "$COLLECTIONS" ]; then
	for collection in $COLLECTIONS ; do
		found="$(echo "$available" | grep "^$collection:")"
		if [ -z "$found" ] ; then
			echo "No such collection '$collection'" >&2
			exit 1
		fi
		TESTS="$TESTS $found"
	done
fi
# Replace available test list with explicitly selected tests.
if [ -n "$TESTS" ]; then
	valid=""
	for test in $TESTS ; do
		found="$(echo "$available" | grep "^${test}$")"
		if [ -z "$found" ] ; then
			echo "No such test '$test'" >&2
			exit 1
		fi
		valid="$valid $found"
	done
	available="$(echo "$valid" | sed -e 's/ /\n/g')"
fi

collections=$(echo "$available" | cut -d: -f1 | sort | uniq)
for collection in $collections ; do
	[ -w /dev/kmsg ] && echo "kselftest: Running tests in $collection" >> /dev/kmsg
	tests=$(echo "$available" | grep "^$collection:" | cut -d: -f2)
	($dryrun cd "$collection" && $dryrun run_many $tests)
done
