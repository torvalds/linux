#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+ or MIT

EXTRA_ARGS=${@}

OLDIFS="$IFS"
IFS=$'\n'
TEST_LIST=(
	"-T s"
	"-T l"
	"-T b"
	"-T b -M"
	"-T m"
	"-T m -M"
	"-T i"
)

TEST_NAME=(
	"spinlock"
	"list"
	"buffer"
	"buffer with barrier"
	"memcpy"
	"memcpy with barrier"
	"increment"
)
IFS="$OLDIFS"

REPS=1000
SLOW_REPS=100

function do_tests()
{
	local i=0
	while [ "$i" -lt "${#TEST_LIST[@]}" ]; do
		echo "Running test ${TEST_NAME[$i]}"
		./param_test ${TEST_LIST[$i]} -r ${REPS} ${@} ${EXTRA_ARGS} || exit 1
		echo "Running compare-twice test ${TEST_NAME[$i]}"
		./param_test_compare_twice ${TEST_LIST[$i]} -r ${REPS} ${@} ${EXTRA_ARGS} || exit 1
		let "i++"
	done
}

echo "Default parameters"
do_tests

echo "Loop injection: 10000 loops"

OLDIFS="$IFS"
IFS=$'\n'
INJECT_LIST=(
	"1"
	"2"
	"3"
	"4"
	"5"
	"6"
	"7"
	"8"
	"9"
)
IFS="$OLDIFS"

NR_LOOPS=10000

i=0
while [ "$i" -lt "${#INJECT_LIST[@]}" ]; do
	echo "Injecting at <${INJECT_LIST[$i]}>"
	do_tests -${INJECT_LIST[i]} ${NR_LOOPS}
	let "i++"
done
NR_LOOPS=

function inject_blocking()
{
	OLDIFS="$IFS"
	IFS=$'\n'
	INJECT_LIST=(
		"7"
		"8"
		"9"
	)
	IFS="$OLDIFS"

	NR_LOOPS=-1

	i=0
	while [ "$i" -lt "${#INJECT_LIST[@]}" ]; do
		echo "Injecting at <${INJECT_LIST[$i]}>"
		do_tests -${INJECT_LIST[i]} -1 ${@}
		let "i++"
	done
	NR_LOOPS=
}

echo "Yield injection (25%)"
inject_blocking -m 4 -y

echo "Yield injection (50%)"
inject_blocking -m 2 -y

echo "Yield injection (100%)"
inject_blocking -m 1 -y

echo "Kill injection (25%)"
inject_blocking -m 4 -k

echo "Kill injection (50%)"
inject_blocking -m 2 -k

echo "Kill injection (100%)"
inject_blocking -m 1 -k

echo "Sleep injection (1ms, 25%)"
inject_blocking -m 4 -s 1

echo "Sleep injection (1ms, 50%)"
inject_blocking -m 2 -s 1

echo "Sleep injection (1ms, 100%)"
inject_blocking -m 1 -s 1
