#!/bin/bash
#
# Copyright (C) 2017 Luis R. Rodriguez <mcgrof@kernel.org>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or at your option any
# later version; or, when distributed separately from the Linux kernel or
# when incorporated into other software packages, subject to the following
# license:
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of copyleft-next (version 0.3.1 or later) as published
# at http://copyleft-next.org/.

# This is a stress test script for kmod, the kernel module loader. It uses
# test_kmod which exposes a series of knobs for the API for us so we can
# tweak each test in userspace rather than in kernelspace.
#
# The way kmod works is it uses the kernel's usermode helper API to eventually
# call /sbin/modprobe. It has a limit of the number of concurrent calls
# possible. The kernel interface to load modules is request_module(), however
# mount uses get_fs_type(). Both behave slightly differently, but the
# differences are important enough to test each call separately. For this
# reason test_kmod starts by providing tests for both calls.
#
# The test driver test_kmod assumes a series of defaults which you can
# override by exporting to your environment prior running this script.
# For instance this script assumes you do not have xfs loaded upon boot.
# If this is false, export DEFAULT_KMOD_FS="ext4" prior to running this
# script if the filesystem module you don't have loaded upon bootup
# is ext4 instead. Refer to allow_user_defaults() for a list of user
# override variables possible.
#
# You'll want at least 4 GiB of RAM to expect to run these tests
# without running out of memory on them. For other requirements refer
# to test_reqs()

set -e

TEST_NAME="kmod"
TEST_DRIVER="test_${TEST_NAME}"
TEST_DIR=$(dirname $0)

# This represents
#
# TEST_ID:TEST_COUNT:ENABLED
#
# TEST_ID: is the test id number
# TEST_COUNT: number of times we should run the test
# ENABLED: 1 if enabled, 0 otherwise
#
# Once these are enabled please leave them as-is. Write your own test,
# we have tons of space.
ALL_TESTS="0001:3:1"
ALL_TESTS="$ALL_TESTS 0002:3:1"
ALL_TESTS="$ALL_TESTS 0003:1:1"
ALL_TESTS="$ALL_TESTS 0004:1:1"
ALL_TESTS="$ALL_TESTS 0005:10:1"
ALL_TESTS="$ALL_TESTS 0006:10:1"
ALL_TESTS="$ALL_TESTS 0007:5:1"
ALL_TESTS="$ALL_TESTS 0008:150:1"
ALL_TESTS="$ALL_TESTS 0009:150:1"
ALL_TESTS="$ALL_TESTS 0010:1:1"
ALL_TESTS="$ALL_TESTS 0011:1:1"
ALL_TESTS="$ALL_TESTS 0012:1:1"
ALL_TESTS="$ALL_TESTS 0013:1:1"

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

test_modprobe()
{
       if [ ! -d $DIR ]; then
               echo "$0: $DIR not present" >&2
               echo "You must have the following enabled in your kernel:" >&2
               cat $TEST_DIR/config >&2
               exit $ksft_skip
       fi
}

function allow_user_defaults()
{
	if [ -z $DEFAULT_KMOD_DRIVER ]; then
		DEFAULT_KMOD_DRIVER="test_module"
	fi

	if [ -z $DEFAULT_KMOD_FS ]; then
		DEFAULT_KMOD_FS="xfs"
	fi

	if [ -z $PROC_DIR ]; then
		PROC_DIR="/proc/sys/kernel/"
	fi

	if [ -z $MODPROBE_LIMIT ]; then
		MODPROBE_LIMIT=50
	fi

	if [ -z $DIR ]; then
		DIR="/sys/devices/virtual/misc/${TEST_DRIVER}0/"
	fi

	if [ -z $DEFAULT_NUM_TESTS ]; then
		DEFAULT_NUM_TESTS=150
	fi

	MODPROBE_LIMIT_FILE="${PROC_DIR}/kmod-limit"
}

test_reqs()
{
	if ! which modprobe 2> /dev/null > /dev/null; then
		echo "$0: You need modprobe installed" >&2
		exit $ksft_skip
	fi

	if ! which kmod 2> /dev/null > /dev/null; then
		echo "$0: You need kmod installed" >&2
		exit $ksft_skip
	fi

	# kmod 19 has a bad bug where it returns 0 when modprobe
	# gets called *even* if the module was not loaded due to
	# some bad heuristics. For details see:
	#
	# A work around is possible in-kernel but its rather
	# complex.
	KMOD_VERSION=$(kmod --version | awk '{print $3}')
	if [[ $KMOD_VERSION  -le 19 ]]; then
		echo "$0: You need at least kmod 20" >&2
		echo "kmod <= 19 is buggy, for details see:" >&2
		echo "http://git.kernel.org/cgit/utils/kernel/kmod/kmod.git/commit/libkmod/libkmod-module.c?id=fd44a98ae2eb5eb32161088954ab21e58e19dfc4" >&2
		exit $ksft_skip
	fi

	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi
}

function load_req_mod()
{
	trap "test_modprobe" EXIT

	if [ ! -d $DIR ]; then
		# Alanis: "Oh isn't it ironic?"
		modprobe $TEST_DRIVER
	fi
}

test_finish()
{
	echo "$MODPROBE" > /proc/sys/kernel/modprobe
	echo "Test completed"
}

errno_name_to_val()
{
	case "$1" in
	# kmod calls modprobe and upon of a module not found
	# modprobe returns just 1... However in the kernel we
	# *sometimes* see 256...
	MODULE_NOT_FOUND)
		echo 256;;
	SUCCESS)
		echo 0;;
	-EPERM)
		echo -1;;
	-ENOENT)
		echo -2;;
	-EINVAL)
		echo -22;;
	-ERR_ANY)
		echo -123456;;
	*)
		echo invalid;;
	esac
}

errno_val_to_name()
	case "$1" in
	256)
		echo MODULE_NOT_FOUND;;
	0)
		echo SUCCESS;;
	-1)
		echo -EPERM;;
	-2)
		echo -ENOENT;;
	-22)
		echo -EINVAL;;
	-123456)
		echo -ERR_ANY;;
	*)
		echo invalid;;
	esac

config_set_test_case_driver()
{
	if ! echo -n 1 >$DIR/config_test_case; then
		echo "$0: Unable to set to test case to driver" >&2
		exit 1
	fi
}

config_set_test_case_fs()
{
	if ! echo -n 2 >$DIR/config_test_case; then
		echo "$0: Unable to set to test case to fs" >&2
		exit 1
	fi
}

config_num_threads()
{
	if ! echo -n $1 >$DIR/config_num_threads; then
		echo "$0: Unable to set to number of threads" >&2
		exit 1
	fi
}

config_get_modprobe_limit()
{
	if [[ -f ${MODPROBE_LIMIT_FILE} ]] ; then
		MODPROBE_LIMIT=$(cat $MODPROBE_LIMIT_FILE)
	fi
	echo $MODPROBE_LIMIT
}

config_num_thread_limit_extra()
{
	MODPROBE_LIMIT=$(config_get_modprobe_limit)
	let EXTRA_LIMIT=$MODPROBE_LIMIT+$1
	config_num_threads $EXTRA_LIMIT
}

# For special characters use printf directly,
# refer to kmod_test_0001
config_set_driver()
{
	if ! echo -n $1 >$DIR/config_test_driver; then
		echo "$0: Unable to set driver" >&2
		exit 1
	fi
}

config_set_fs()
{
	if ! echo -n $1 >$DIR/config_test_fs; then
		echo "$0: Unable to set driver" >&2
		exit 1
	fi
}

config_get_driver()
{
	cat $DIR/config_test_driver
}

config_get_test_result()
{
	cat $DIR/test_result
}

config_reset()
{
	if ! echo -n "1" >"$DIR"/reset; then
		echo "$0: reset should have worked" >&2
		exit 1
	fi
}

config_show_config()
{
	echo "----------------------------------------------------"
	cat "$DIR"/config
	echo "----------------------------------------------------"
}

config_trigger()
{
	if ! echo -n "1" >"$DIR"/trigger_config 2>/dev/null; then
		echo "$1: FAIL - loading should have worked"
		config_show_config
		exit 1
	fi
	echo "$1: OK! - loading kmod test"
}

config_trigger_want_fail()
{
	if echo "1" > $DIR/trigger_config 2>/dev/null; then
		echo "$1: FAIL - test case was expected to fail"
		config_show_config
		exit 1
	fi
	echo "$1: OK! - kmod test case failed as expected"
}

config_expect_result()
{
	RC=$(config_get_test_result)
	RC_NAME=$(errno_val_to_name $RC)

	ERRNO_NAME=$2
	ERRNO=$(errno_name_to_val $ERRNO_NAME)

	if [[ $ERRNO_NAME = "-ERR_ANY" ]]; then
		if [[ $RC -ge 0 ]]; then
			echo "$1: FAIL, test expects $ERRNO_NAME - got $RC_NAME ($RC)" >&2
			config_show_config
			exit 1
		fi
	elif [[ $RC != $ERRNO ]]; then
		echo "$1: FAIL, test expects $ERRNO_NAME ($ERRNO) - got $RC_NAME ($RC)" >&2
		config_show_config
		exit 1
	fi
	echo "$1: OK! - Return value: $RC ($RC_NAME), expected $ERRNO_NAME"
}

kmod_defaults_driver()
{
	config_reset
	modprobe -r $DEFAULT_KMOD_DRIVER
	config_set_driver $DEFAULT_KMOD_DRIVER
}

kmod_defaults_fs()
{
	config_reset
	modprobe -r $DEFAULT_KMOD_FS
	config_set_fs $DEFAULT_KMOD_FS
	config_set_test_case_fs
}

kmod_test_0001_driver()
{
	NAME='\000'

	kmod_defaults_driver
	config_num_threads 1
	printf '\000' >"$DIR"/config_test_driver
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} MODULE_NOT_FOUND
}

kmod_test_0001_fs()
{
	NAME='\000'

	kmod_defaults_fs
	config_num_threads 1
	printf '\000' >"$DIR"/config_test_fs
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} -EINVAL
}

kmod_test_0001()
{
	kmod_test_0001_driver
	kmod_test_0001_fs
}

kmod_test_0002_driver()
{
	NAME="nope-$DEFAULT_KMOD_DRIVER"

	kmod_defaults_driver
	config_set_driver $NAME
	config_num_threads 1
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} MODULE_NOT_FOUND
}

kmod_test_0002_fs()
{
	NAME="nope-$DEFAULT_KMOD_FS"

	kmod_defaults_fs
	config_set_fs $NAME
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} -EINVAL
}

kmod_test_0002()
{
	kmod_test_0002_driver
	kmod_test_0002_fs
}

kmod_test_0003()
{
	kmod_defaults_fs
	config_num_threads 1
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} SUCCESS
}

kmod_test_0004()
{
	kmod_defaults_fs
	config_num_threads 2
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} SUCCESS
}

kmod_test_0005()
{
	kmod_defaults_driver
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} SUCCESS
}

kmod_test_0006()
{
	kmod_defaults_fs
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} SUCCESS
}

kmod_test_0007()
{
	kmod_test_0005
	kmod_test_0006
}

kmod_test_0008()
{
	kmod_defaults_driver
	MODPROBE_LIMIT=$(config_get_modprobe_limit)
	let EXTRA=$MODPROBE_LIMIT/6
	config_num_thread_limit_extra $EXTRA
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} SUCCESS
}

kmod_test_0009()
{
	kmod_defaults_fs
	MODPROBE_LIMIT=$(config_get_modprobe_limit)
	let EXTRA=$MODPROBE_LIMIT/4
	config_num_thread_limit_extra $EXTRA
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} SUCCESS
}

kmod_test_0010()
{
	kmod_defaults_driver
	config_num_threads 1
	echo "/KMOD_TEST_NONEXISTENT" > /proc/sys/kernel/modprobe
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} -ENOENT
	echo "$MODPROBE" > /proc/sys/kernel/modprobe
}

kmod_test_0011()
{
	kmod_defaults_driver
	config_num_threads 1
	# This causes the kernel to not even try executing modprobe.  The error
	# code is still -ENOENT like when modprobe doesn't exist, so we can't
	# easily test for the exact difference.  But this still is a useful test
	# since there was a bug where request_module() returned 0 in this case.
	echo > /proc/sys/kernel/modprobe
	config_trigger ${FUNCNAME[0]}
	config_expect_result ${FUNCNAME[0]} -ENOENT
	echo "$MODPROBE" > /proc/sys/kernel/modprobe
}

kmod_check_visibility()
{
	local name="$1"
	local cmd="$2"

	modprobe $DEFAULT_KMOD_DRIVER

	local priv=$(eval $cmd)
	local unpriv=$(capsh --drop=CAP_SYSLOG -- -c "$cmd")

	if [ "$priv" = "$unpriv" ] || \
	   [ "${priv:0:3}" = "0x0" ] || \
	   [ "${unpriv:0:3}" != "0x0" ] ; then
		echo "${FUNCNAME[0]}: FAIL, $name visible to unpriv: '$priv' vs '$unpriv'" >&2
		exit 1
	else
		echo "${FUNCNAME[0]}: OK!"
	fi
}

kmod_test_0012()
{
	kmod_check_visibility /proc/modules \
		"grep '^${DEFAULT_KMOD_DRIVER}\b' /proc/modules | awk '{print \$NF}'"
}

kmod_test_0013()
{
	kmod_check_visibility '/sys/module/*/sections/*' \
		"cat /sys/module/${DEFAULT_KMOD_DRIVER}/sections/.*text | head -n1"
}

list_tests()
{
	echo "Test ID list:"
	echo
	echo "TEST_ID x NUM_TEST"
	echo "TEST_ID:   Test ID"
	echo "NUM_TESTS: Number of recommended times to run the test"
	echo
	echo "0001 x $(get_test_count 0001) - Simple test - 1 thread  for empty string"
	echo "0002 x $(get_test_count 0002) - Simple test - 1 thread  for modules/filesystems that do not exist"
	echo "0003 x $(get_test_count 0003) - Simple test - 1 thread  for get_fs_type() only"
	echo "0004 x $(get_test_count 0004) - Simple test - 2 threads for get_fs_type() only"
	echo "0005 x $(get_test_count 0005) - multithreaded tests with default setup - request_module() only"
	echo "0006 x $(get_test_count 0006) - multithreaded tests with default setup - get_fs_type() only"
	echo "0007 x $(get_test_count 0007) - multithreaded tests with default setup test request_module() and get_fs_type()"
	echo "0008 x $(get_test_count 0008) - multithreaded - push kmod_concurrent over max_modprobes for request_module()"
	echo "0009 x $(get_test_count 0009) - multithreaded - push kmod_concurrent over max_modprobes for get_fs_type()"
	echo "0010 x $(get_test_count 0010) - test nonexistent modprobe path"
	echo "0011 x $(get_test_count 0011) - test completely disabling module autoloading"
	echo "0012 x $(get_test_count 0012) - test /proc/modules address visibility under CAP_SYSLOG"
	echo "0013 x $(get_test_count 0013) - test /sys/module/*/sections/* visibility under CAP_SYSLOG"
}

usage()
{
	NUM_TESTS=$(grep -o ' ' <<<"$ALL_TESTS" | grep -c .)
	let NUM_TESTS=$NUM_TESTS+1
	MAX_TEST=$(printf "%04d\n" $NUM_TESTS)
	echo "Usage: $0 [ -t <4-number-digit> ] | [ -w <4-number-digit> ] |"
	echo "		 [ -s <4-number-digit> ] | [ -c <4-number-digit> <test- count>"
	echo "           [ all ] [ -h | --help ] [ -l ]"
	echo ""
	echo "Valid tests: 0001-$MAX_TEST"
	echo ""
	echo "    all     Runs all tests (default)"
	echo "    -t      Run test ID the number amount of times is recommended"
	echo "    -w      Watch test ID run until it runs into an error"
	echo "    -s      Run test ID once"
	echo "    -c      Run test ID x test-count number of times"
	echo "    -l      List all test ID list"
	echo " -h|--help  Help"
	echo
	echo "If an error every occurs execution will immediately terminate."
	echo "If you are adding a new test try using -w <test-ID> first to"
	echo "make sure the test passes a series of tests."
	echo
	echo Example uses:
	echo
	echo "${TEST_NAME}.sh		-- executes all tests"
	echo "${TEST_NAME}.sh -t 0008	-- Executes test ID 0008 number of times is recommended"
	echo "${TEST_NAME}.sh -w 0008	-- Watch test ID 0008 run until an error occurs"
	echo "${TEST_NAME}.sh -s 0008	-- Run test ID 0008 once"
	echo "${TEST_NAME}.sh -c 0008 3	-- Run test ID 0008 three times"
	echo
	list_tests
	exit 1
}

function test_num()
{
	re='^[0-9]+$'
	if ! [[ $1 =~ $re ]]; then
		usage
	fi
}

function get_test_data()
{
	test_num $1
	local field_num=$(echo $1 | sed 's/^0*//')
	echo $ALL_TESTS | awk '{print $'$field_num'}'
}

function get_test_count()
{
	TEST_DATA=$(get_test_data $1)
	LAST_TWO=${TEST_DATA#*:*}
	echo ${LAST_TWO%:*}
}

function get_test_enabled()
{
	TEST_DATA=$(get_test_data $1)
	echo ${TEST_DATA#*:*:}
}

function run_all_tests()
{
	for i in $ALL_TESTS ; do
		TEST_ID=${i%:*:*}
		ENABLED=$(get_test_enabled $TEST_ID)
		TEST_COUNT=$(get_test_count $TEST_ID)
		if [[ $ENABLED -eq "1" ]]; then
			test_case $TEST_ID $TEST_COUNT
		fi
	done
}

function watch_log()
{
	if [ $# -ne 3 ]; then
		clear
	fi
	date
	echo "Running test: $2 - run #$1"
}

function watch_case()
{
	i=0
	while [ 1 ]; do

		if [ $# -eq 1 ]; then
			test_num $1
			watch_log $i ${TEST_NAME}_test_$1
			${TEST_NAME}_test_$1
		else
			watch_log $i all
			run_all_tests
		fi
		let i=$i+1
	done
}

function test_case()
{
	NUM_TESTS=$DEFAULT_NUM_TESTS
	if [ $# -eq 2 ]; then
		NUM_TESTS=$2
	fi

	i=0
	while [ $i -lt $NUM_TESTS ]; do
		test_num $1
		watch_log $i ${TEST_NAME}_test_$1 noclear
		RUN_TEST=${TEST_NAME}_test_$1
		$RUN_TEST
		let i=$i+1
	done
}

function parse_args()
{
	if [ $# -eq 0 ]; then
		run_all_tests
	else
		if [[ "$1" = "all" ]]; then
			run_all_tests
		elif [[ "$1" = "-w" ]]; then
			shift
			watch_case $@
		elif [[ "$1" = "-t" ]]; then
			shift
			test_num $1
			test_case $1 $(get_test_count $1)
		elif [[ "$1" = "-c" ]]; then
			shift
			test_num $1
			test_num $2
			test_case $1 $2
		elif [[ "$1" = "-s" ]]; then
			shift
			test_case $1 1
		elif [[ "$1" = "-l" ]]; then
			list_tests
		elif [[ "$1" = "-h" || "$1" = "--help" ]]; then
			usage
		else
			usage
		fi
	fi
}

test_reqs
allow_user_defaults
load_req_mod

MODPROBE=$(</proc/sys/kernel/modprobe)
trap "test_finish" EXIT

parse_args $@

exit 0
