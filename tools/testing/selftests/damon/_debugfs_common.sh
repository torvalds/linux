#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

test_write_result() {
	file=$1
	content=$2
	orig_content=$3
	expect_reason=$4
	expected=$5

	echo "$content" > "$file"
	if [ $? -ne "$expected" ]
	then
		echo "writing $content to $file doesn't return $expected"
		echo "expected because: $expect_reason"
		echo "$orig_content" > "$file"
		exit 1
	fi
}

test_write_succ() {
	test_write_result "$1" "$2" "$3" "$4" 0
}

test_write_fail() {
	test_write_result "$1" "$2" "$3" "$4" 1
}

test_content() {
	file=$1
	orig_content=$2
	expected=$3
	expect_reason=$4

	content=$(cat "$file")
	if [ "$content" != "$expected" ]
	then
		echo "reading $file expected $expected but $content"
		echo "expected because: $expect_reason"
		echo "$orig_content" > "$file"
		exit 1
	fi
}

source ./_chk_dependency.sh

damon_onoff="$DBGFS/monitor_on"
if [ -f "$DBGFS/monitor_on_DEPRECATED" ]
then
	damon_onoff="$DBGFS/monitor_on_DEPRECATED"
else
	damon_onoff="$DBGFS/monitor_on"
fi

if [ $(cat "$damon_onoff") = "on" ]
then
	echo "monitoring is on"
	exit $ksft_skip
fi
