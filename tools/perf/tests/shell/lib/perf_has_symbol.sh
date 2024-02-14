#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

perf_has_symbol()
{
	if perf test -vv "Symbols" 2>&1 | grep "[[:space:]]$1$"; then
		echo "perf does have symbol '$1'"
		return 0
	fi
	echo "perf does not have symbol '$1'"
	return 1
}

skip_test_missing_symbol()
{
	if ! perf_has_symbol "$1" ; then
		echo "perf is missing symbols - skipping test"
		exit 2
	fi
	return 0
}
