#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later OR copyleft-next-0.3.1
# Copyright (C) 2017 Luis R. Rodriguez <mcgrof@kernel.org>

# This performs a series tests against the proc sysctl interface.

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

TEST_NAME="sysctl"
TEST_DRIVER="test_${TEST_NAME}"
TEST_DIR=$(dirname $0)
TEST_FILE=$(mktemp)

# This represents
#
# TEST_ID:TEST_COUNT:ENABLED:TARGET:SKIP_NO_TARGET
#
# TEST_ID: is the test id number
# TEST_COUNT: number of times we should run the test
# ENABLED: 1 if enabled, 0 otherwise
# TARGET: test target file required on the test_sysctl module
# SKIP_NO_TARGET: 1 skip if TARGET not there
#                 0 run even though TARGET not there
#
# Once these are enabled please leave them as-is. Write your own test,
# we have tons of space.
ALL_TESTS="0001:1:1:int_0001:1"
ALL_TESTS="$ALL_TESTS 0002:1:1:string_0001:1"
ALL_TESTS="$ALL_TESTS 0003:1:1:int_0002:1"
ALL_TESTS="$ALL_TESTS 0004:1:1:uint_0001:1"
ALL_TESTS="$ALL_TESTS 0005:3:1:int_0003:1"
ALL_TESTS="$ALL_TESTS 0006:50:1:bitmap_0001:1"
ALL_TESTS="$ALL_TESTS 0007:1:1:boot_int:1"
ALL_TESTS="$ALL_TESTS 0008:1:1:match_int:1"
ALL_TESTS="$ALL_TESTS 0009:1:1:unregister_error:0"
ALL_TESTS="$ALL_TESTS 0010:1:1:mnt/mnt_error:0"
ALL_TESTS="$ALL_TESTS 0011:1:1:empty_add:0"
ALL_TESTS="$ALL_TESTS 0012:1:1:u8_valid:0"

function allow_user_defaults()
{
	if [ -z $DIR ]; then
		DIR="/sys/module/test_sysctl/"
	fi
	if [ -z $DEFAULT_NUM_TESTS ]; then
		DEFAULT_NUM_TESTS=50
	fi
	if [ -z $SYSCTL ]; then
		SYSCTL="/proc/sys/debug/test_sysctl"
	fi
	if [ -z $PROD_SYSCTL ]; then
		PROD_SYSCTL="/proc/sys"
	fi
	if [ -z $WRITES_STRICT ]; then
		WRITES_STRICT="${PROD_SYSCTL}/kernel/sysctl_writes_strict"
	fi
}

function check_production_sysctl_writes_strict()
{
	echo -n "Checking production write strict setting ... "
	if [ ! -e ${WRITES_STRICT} ]; then
		echo "FAIL, but skip in case of old kernel" >&2
	else
		old_strict=$(cat ${WRITES_STRICT})
		if [ "$old_strict" = "1" ]; then
			echo "OK"
		else
			echo "FAIL, strict value is 0 but force to 1 to continue" >&2
			echo "1" > ${WRITES_STRICT}
		fi
	fi

	if [ -z $PAGE_SIZE ]; then
		PAGE_SIZE=$(getconf PAGESIZE)
	fi
	if [ -z $MAX_DIGITS ]; then
		MAX_DIGITS=$(($PAGE_SIZE/8))
	fi
	if [ -z $INT_MAX ]; then
		INT_MAX=$(getconf INT_MAX)
	fi
	if [ -z $UINT_MAX ]; then
		UINT_MAX=$(getconf UINT_MAX)
	fi
}

test_reqs()
{
	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi

	if ! which perl 2> /dev/null > /dev/null; then
		echo "$0: You need perl installed"
		exit $ksft_skip
	fi
	if ! which getconf 2> /dev/null > /dev/null; then
		echo "$0: You need getconf installed"
		exit $ksft_skip
	fi
	if ! which diff 2> /dev/null > /dev/null; then
		echo "$0: You need diff installed"
		exit $ksft_skip
	fi
}

function load_req_mod()
{
	if [ ! -d $SYSCTL ]; then
		if ! modprobe -q -n $TEST_DRIVER; then
			echo "$0: module $TEST_DRIVER not found [SKIP]"
			echo "You must set CONFIG_TEST_SYSCTL=m in your kernel" >&2
			exit $ksft_skip
		fi
		modprobe $TEST_DRIVER
		if [ $? -ne 0 ]; then
			echo "$0: modprobe $TEST_DRIVER failed."
			exit
		fi
	fi
}

reset_vals()
{
	VAL=""
	TRIGGER=$(basename ${TARGET})
	case "$TRIGGER" in
		int_0001)
			VAL="60"
			;;
		int_0002)
			VAL="1"
			;;
		uint_0001)
			VAL="314"
			;;
		string_0001)
			VAL="(none)"
			;;
		bitmap_0001)
			VAL=""
			;;
		*)
			;;
	esac
	echo -n $VAL > $TARGET
}

set_orig()
{
	if [ ! -z $TARGET ] && [ ! -z $ORIG ]; then
		if [ -f ${TARGET} ]; then
			echo "${ORIG}" > "${TARGET}"
		fi
	fi
}

set_test()
{
	echo "${TEST_STR}" > "${TARGET}"
}

verify()
{
	local seen
	seen=$(cat "$1")
	if [ "${seen}" != "${TEST_STR}" ]; then
		return 1
	fi
	return 0
}

# proc files get read a page at a time, which can confuse diff,
# and get you incorrect results on proc files with long data. To use
# diff against them you must first extract the output to a file, and
# then compare against that file.
verify_diff_proc_file()
{
	TMP_DUMP_FILE=$(mktemp)
	cat $1 > $TMP_DUMP_FILE

	if ! diff -w -q $TMP_DUMP_FILE $2; then
		return 1
	else
		return 0
	fi
}

verify_diff_w()
{
	echo "$TEST_STR" | diff -q -w -u - $1 > /dev/null
	return $?
}

test_rc()
{
	if [[ $rc != 0 ]]; then
		echo "Failed test, return value: $rc" >&2
		exit $rc
	fi
}

test_finish()
{
	set_orig
	rm -f "${TEST_FILE}"

	if [ ! -z ${old_strict} ]; then
		echo ${old_strict} > ${WRITES_STRICT}
	fi
	exit $rc
}

run_numerictests()
{
	echo "== Testing sysctl behavior against ${TARGET} =="

	rc=0

	echo -n "Writing test file ... "
	echo "${TEST_STR}" > "${TEST_FILE}"
	if ! verify "${TEST_FILE}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "OK"
	fi

	echo -n "Checking sysctl is not set to test value ... "
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "OK"
	fi

	echo -n "Writing sysctl from shell ... "
	set_test
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "OK"
	fi

	echo -n "Resetting sysctl to original value ... "
	set_orig
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "OK"
	fi

	# Now that we've validated the sanity of "set_test" and "set_orig",
	# we can use those functions to set starting states before running
	# specific behavioral tests.

	echo -n "Writing entire sysctl in single write ... "
	set_orig
	dd if="${TEST_FILE}" of="${TARGET}" bs=4096 2>/dev/null
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi

	echo -n "Writing middle of sysctl after synchronized seek ... "
	set_test
	dd if="${TEST_FILE}" of="${TARGET}" bs=1 seek=1 skip=1 2>/dev/null
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi

	echo -n "Writing beyond end of sysctl ... "
	set_orig
	dd if="${TEST_FILE}" of="${TARGET}" bs=20 seek=2 2>/dev/null
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi

	echo -n "Writing sysctl with multiple long writes ... "
	set_orig
	(perl -e 'print "A" x 50;'; echo "${TEST_STR}") | \
		dd of="${TARGET}" bs=50 2>/dev/null
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc
}

check_failure()
{
	echo -n "Testing that $1 fails as expected ... "
	reset_vals
	TEST_STR="$1"
	orig="$(cat $TARGET)"
	echo -n "$TEST_STR" > $TARGET 2> /dev/null

	# write should fail and $TARGET should retain its original value
	if [ $? = 0 ] || [ "$(cat $TARGET)" != "$orig" ]; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc
}

run_wideint_tests()
{
	# sysctl conversion functions receive a boolean sign and ulong
	# magnitude; here we list the magnitudes we want to test (each of
	# which will be tested in both positive and negative forms).  Since
	# none of these values fit in 32 bits, writing them to an int- or
	# uint-typed sysctl should fail.
	local magnitudes=(
		# common boundary-condition values (zero, +1, -1, INT_MIN,
		# and INT_MAX respectively) if truncated to lower 32 bits
		# (potential for being falsely deemed in range)
		0x0000000100000000
		0x0000000100000001
		0x00000001ffffffff
		0x0000000180000000
		0x000000017fffffff

		# these look like negatives, but without a leading '-' are
		# actually large positives (should be rejected as above
		# despite being zero/+1/-1/INT_MIN/INT_MAX in the lower 32)
		0xffffffff00000000
		0xffffffff00000001
		0xffffffffffffffff
		0xffffffff80000000
		0xffffffff7fffffff
	)

	for sign in '' '-'; do
		for mag in "${magnitudes[@]}"; do
			check_failure "${sign}${mag}"
		done
	done
}

# Your test must accept digits 3 and 4 to use this
run_limit_digit()
{
	echo -n "Checking ignoring spaces up to PAGE_SIZE works on write ... "
	reset_vals

	LIMIT=$((MAX_DIGITS -1))
	TEST_STR="3"
	(perl -e 'print " " x '$LIMIT';'; echo "${TEST_STR}") | \
		dd of="${TARGET}" 2>/dev/null

	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc

	echo -n "Checking passing PAGE_SIZE of spaces fails on write ... "
	reset_vals

	LIMIT=$((MAX_DIGITS))
	TEST_STR="4"
	(perl -e 'print " " x '$LIMIT';'; echo "${TEST_STR}") | \
		dd of="${TARGET}" 2>/dev/null

	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc
}

# You are using an int
run_limit_digit_int()
{
	echo -n "Testing INT_MAX works ... "
	reset_vals
	TEST_STR="$INT_MAX"
	echo -n $TEST_STR > $TARGET

	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc

	echo -n "Testing INT_MAX + 1 will fail as expected ... "
	reset_vals
	let TEST_STR=$INT_MAX+1
	echo -n $TEST_STR > $TARGET 2> /dev/null

	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc

	echo -n "Testing negative values will work as expected ... "
	reset_vals
	TEST_STR="-3"
	echo -n $TEST_STR > $TARGET 2> /dev/null
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc
}

# You used an int array
run_limit_digit_int_array()
{
	echo -n "Testing array works as expected ... "
	TEST_STR="4 3 2 1"
	echo -n $TEST_STR > $TARGET

	if ! verify_diff_w "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc

	echo -n "Testing skipping trailing array elements works ... "
	# Do not reset_vals, carry on the values from the last test.
	# If we only echo in two digits the last two are left intact
	TEST_STR="100 101"
	echo -n $TEST_STR > $TARGET
	# After we echo in, to help diff we need to set on TEST_STR what
	# we expect the result to be.
	TEST_STR="100 101 2 1"

	if ! verify_diff_w "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc

	echo -n "Testing PAGE_SIZE limit on array works ... "
	# Do not reset_vals, carry on the values from the last test.
	# Even if you use an int array, you are still restricted to
	# MAX_DIGITS, this is a known limitation. Test limit works.
	LIMIT=$((MAX_DIGITS -1))
	TEST_STR="9"
	(perl -e 'print " " x '$LIMIT';'; echo "${TEST_STR}") | \
		dd of="${TARGET}" 2>/dev/null

	TEST_STR="9 101 2 1"
	if ! verify_diff_w "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc

	echo -n "Testing exceeding PAGE_SIZE limit fails as expected ... "
	# Do not reset_vals, carry on the values from the last test.
	# Now go over limit.
	LIMIT=$((MAX_DIGITS))
	TEST_STR="7"
	(perl -e 'print " " x '$LIMIT';'; echo "${TEST_STR}") | \
		dd of="${TARGET}" 2>/dev/null

	TEST_STR="7 101 2 1"
	if verify_diff_w "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc
}

# You are using an unsigned int
run_limit_digit_uint()
{
	echo -n "Testing UINT_MAX works ... "
	reset_vals
	TEST_STR="$UINT_MAX"
	echo -n $TEST_STR > $TARGET

	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc

	echo -n "Testing UINT_MAX + 1 will fail as expected ... "
	reset_vals
	TEST_STR=$(($UINT_MAX+1))
	echo -n $TEST_STR > $TARGET 2> /dev/null

	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc

	echo -n "Testing negative values will not work as expected ... "
	reset_vals
	TEST_STR="-3"
	echo -n $TEST_STR > $TARGET 2> /dev/null

	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi
	test_rc
}

run_stringtests()
{
	echo -n "Writing entire sysctl in short writes ... "
	set_orig
	dd if="${TEST_FILE}" of="${TARGET}" bs=1 2>/dev/null
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi

	echo -n "Writing middle of sysctl after unsynchronized seek ... "
	set_test
	dd if="${TEST_FILE}" of="${TARGET}" bs=1 seek=1 2>/dev/null
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi

	echo -n "Checking sysctl maxlen is at least $MAXLEN ... "
	set_orig
	perl -e 'print "A" x ('"${MAXLEN}"'-2), "B";' | \
		dd of="${TARGET}" bs="${MAXLEN}" 2>/dev/null
	if ! grep -q B "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi

	echo -n "Checking sysctl keeps original string on overflow append ... "
	set_orig
	perl -e 'print "A" x ('"${MAXLEN}"'-1), "B";' | \
		dd of="${TARGET}" bs=$(( MAXLEN - 1 )) 2>/dev/null
	if grep -q B "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi

	echo -n "Checking sysctl stays NULL terminated on write ... "
	set_orig
	perl -e 'print "A" x ('"${MAXLEN}"'-1), "B";' | \
		dd of="${TARGET}" bs="${MAXLEN}" 2>/dev/null
	if grep -q B "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi

	echo -n "Checking sysctl stays NULL terminated on overwrite ... "
	set_orig
	perl -e 'print "A" x ('"${MAXLEN}"'-1), "BB";' | \
		dd of="${TARGET}" bs=$(( $MAXLEN + 1 )) 2>/dev/null
	if grep -q B "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
	fi

	test_rc
}

target_exists()
{
	TARGET="${SYSCTL}/$1"
	TEST_ID="$2"

	if [ ! -f ${TARGET} ] ; then
		return 0
	fi
	return 1
}

run_bitmaptest() {
	# Total length of bitmaps string to use, a bit under
	# the maximum input size of the test node
	LENGTH=$((RANDOM % 65000))

	# First bit to set
	BIT=$((RANDOM % 1024))

	# String containing our list of bits to set
	TEST_STR=$BIT

	# build up the string
	while [ "${#TEST_STR}" -le "$LENGTH" ]; do
		# Make sure next entry is discontiguous,
		# skip ahead at least 2
		BIT=$((BIT + $((2 + RANDOM % 10))))

		# Add new bit to the list
		TEST_STR="${TEST_STR},${BIT}"

		# Randomly make it a range
		if [ "$((RANDOM % 2))" -eq "1" ]; then
			RANGE_END=$((BIT + $((1 + RANDOM % 10))))
			TEST_STR="${TEST_STR}-${RANGE_END}"
			BIT=$RANGE_END
		fi
	done

	echo -n "Checking bitmap handler ... "
	TEST_FILE=$(mktemp)
	echo -n "$TEST_STR" > $TEST_FILE

	cat $TEST_FILE > $TARGET 2> /dev/null
	if [ $? -ne 0 ]; then
		echo "FAIL" >&2
		rc=1
		test_rc
	fi

	if ! verify_diff_proc_file "$TARGET" "$TEST_FILE"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "OK"
		rc=0
	fi
	test_rc
}

sysctl_test_0001()
{
	TARGET="${SYSCTL}/$(get_test_target 0001)"
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	run_numerictests
	run_wideint_tests
	run_limit_digit
}

sysctl_test_0002()
{
	TARGET="${SYSCTL}/$(get_test_target 0002)"
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR="Testing sysctl"
	# Only string sysctls support seeking/appending.
	MAXLEN=65

	run_numerictests
	run_stringtests
}

sysctl_test_0003()
{
	TARGET="${SYSCTL}/$(get_test_target 0003)"
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	run_numerictests
	run_wideint_tests
	run_limit_digit
	run_limit_digit_int
}

sysctl_test_0004()
{
	TARGET="${SYSCTL}/$(get_test_target 0004)"
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	run_numerictests
	run_wideint_tests
	run_limit_digit
	run_limit_digit_uint
}

sysctl_test_0005()
{
	TARGET="${SYSCTL}/$(get_test_target 0005)"
	reset_vals
	ORIG=$(cat "${TARGET}")

	run_limit_digit_int_array
}

sysctl_test_0006()
{
	TARGET="${SYSCTL}/$(get_test_target 0006)"
	reset_vals
	ORIG=""
	run_bitmaptest
}

sysctl_test_0007()
{
	TARGET="${SYSCTL}/$(get_test_target 0007)"
	echo -n "Testing if $TARGET is set to 1 ... "

	if [ ! -f $TARGET ]; then
		echo -e "SKIPPING\n$TARGET is not present"
		return $ksft_skip
	fi

	if [ -d $DIR ]; then
		echo -e "SKIPPING\nTest only possible if sysctl_test is built-in, not module:"
		cat $TEST_DIR/config >&2
		return $ksft_skip
	fi

	ORIG=$(cat "${TARGET}")

	if [ x$ORIG = "x1" ]; then
		echo "OK"
		return 0
	fi

	if [ ! -f /proc/cmdline ]; then
		echo -e "SKIPPING\nThere is no /proc/cmdline to check for parameter"
		return $ksft_skip
	fi

	FOUND=$(grep -c "sysctl[./]debug[./]test_sysctl[./]boot_int=1" /proc/cmdline)
	if [ $FOUND = "1" ]; then
		echo -e "FAIL\nKernel param found but $TARGET is not 1." >&2
		rc=1
		test_rc
	fi

	echo -e "SKIPPING\nExpected kernel parameter missing."
	echo "Kernel must be booted with parameter: sysctl.debug.test_sysctl.boot_int=1"
	return $ksft_skip
}

sysctl_test_0008()
{
	TARGET="${SYSCTL}/$(get_test_target 0008)"
	echo -n "Testing if $TARGET is matched in kernel ... "

	if [ ! -f $TARGET ]; then
		echo -e "SKIPPING\n$TARGET is not present"
		return $ksft_skip
	fi

	ORIG_VALUE=$(cat "${TARGET}")

	if [ $ORIG_VALUE -ne 1 ]; then
		echo "FAIL" >&2
		rc=1
		test_rc
	fi

	echo "OK"
	return 0
}

sysctl_test_0009()
{
	TARGET="${SYSCTL}/$(get_test_target 0009)"
	echo -n "Testing if $TARGET unregistered correctly ... "
	if [ -d $TARGET ]; then
		echo "FAIL" >&2
		rc=1
		test_rc
	fi

	echo "OK"
	return 0
}

sysctl_test_0010()
{
	TARGET="${SYSCTL}/$(get_test_target 0010)"
	echo -n "Testing that $TARGET was not created ... "
	if [ -d $TARGET ]; then
		echo "FAIL" >&2
		rc=1
		test_rc
	fi

	echo "OK"
	return 0
}

sysctl_test_0011()
{
	TARGET="${SYSCTL}/$(get_test_target 0011)"
	echo -n "Testing empty dir handling in ${TARGET} ... "
	if [ ! -d ${TARGET} ]; then
		echo -e "FAIL\nCould not create ${TARGET}" >&2
		rc=1
		test_rc
	fi

	TARGET2="${TARGET}/empty"
	if [ ! -d ${TARGET2} ]; then
		echo -e "FAIL\nCould not create ${TARGET2}" >&2
		rc=1
		test_rc
	fi

	echo "OK"
	return 0
}

sysctl_test_0012()
{
	TARGET="${SYSCTL}/$(get_test_target 0012)"
	echo -n "Testing u8 range check in sysctl table check in ${TARGET} ... "
	if [ ! -f ${TARGET} ]; then
		echo -e "FAIL\nCould not create ${TARGET}" >&2
		rc=1
		test_rc
	fi

	local u8over_msg=$(dmesg | grep "u8_over range value" | wc -l)
	if [ ! ${u8over_msg} -eq 1 ]; then
		echo -e "FAIL\nu8 overflow not detected" >&2
		rc=1
		test_rc
	fi

	local u8under_msg=$(dmesg | grep "u8_under range value" | wc -l)
	if [ ! ${u8under_msg} -eq 1 ]; then
		echo -e "FAIL\nu8 underflow not detected" >&2
		rc=1
		test_rc
	fi

	echo "OK"
	return 0
}

list_tests()
{
	echo "Test ID list:"
	echo
	echo "TEST_ID x NUM_TEST"
	echo "TEST_ID:   Test ID"
	echo "NUM_TESTS: Recommended number of times to run the test"
	echo
	echo "0001 x $(get_test_count 0001) - tests proc_dointvec_minmax()"
	echo "0002 x $(get_test_count 0002) - tests proc_dostring()"
	echo "0003 x $(get_test_count 0003) - tests proc_dointvec()"
	echo "0004 x $(get_test_count 0004) - tests proc_douintvec()"
	echo "0005 x $(get_test_count 0005) - tests proc_douintvec() array"
	echo "0006 x $(get_test_count 0006) - tests proc_do_large_bitmap()"
	echo "0007 x $(get_test_count 0007) - tests setting sysctl from kernel boot param"
	echo "0008 x $(get_test_count 0008) - tests sysctl macro values match"
	echo "0009 x $(get_test_count 0009) - tests sysct unregister"
	echo "0010 x $(get_test_count 0010) - tests sysct mount point"
	echo "0011 x $(get_test_count 0011) - tests empty directories"
	echo "0012 x $(get_test_count 0012) - tests range check for u8 proc_handler"
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
	echo "    -t      Run test ID the recommended number of times"
	echo "    -w      Watch test ID run until it runs into an error"
	echo "    -c      Run test ID once"
	echo "    -s      Run test ID x test-count number of times"
	echo "    -l      List all test ID list"
	echo " -h|--help  Help"
	echo
	echo "If an error every occurs execution will immediately terminate."
	echo "If you are adding a new test try using -w <test-ID> first to"
	echo "make sure the test passes a series of tests."
	echo
	echo Example uses:
	echo
	echo "$TEST_NAME.sh            -- executes all tests"
	echo "$TEST_NAME.sh -t 0002    -- Executes test ID 0002 the recommended number of times"
	echo "$TEST_NAME.sh -w 0002    -- Watch test ID 0002 run until an error occurs"
	echo "$TEST_NAME.sh -s 0002    -- Run test ID 0002 once"
	echo "$TEST_NAME.sh -c 0002 3  -- Run test ID 0002 three times"
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
function remove_leading_zeros()
{
	echo $1 | sed 's/^0*//'
}

function get_test_count()
{
	test_num $1
	awk_field=$(remove_leading_zeros $1)
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$awk_field'}')
	echo ${TEST_DATA} | awk -F":" '{print $2}'
}

function get_test_enabled()
{
	test_num $1
	awk_field=$(remove_leading_zeros $1)
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$awk_field'}')
	echo ${TEST_DATA} | awk -F":" '{print $3}'
}

function get_test_target()
{
	test_num $1
	awk_field=$(remove_leading_zeros $1)
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$awk_field'}')
	echo ${TEST_DATA} | awk -F":" '{print $4}'
}

function get_test_skip_no_target()
{
	test_num $1
	awk_field=$(remove_leading_zeros $1)
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$awk_field'}')
	echo ${TEST_DATA} | awk -F":" '{print $5}'
}

function skip_test()
{
	TEST_ID=$1
	TEST_TARGET=$2
	if target_exists $TEST_TARGET $TEST_ID; then
		TEST_SKIP=$(get_test_skip_no_target $TEST_ID)
		if [[ $TEST_SKIP -eq "1" ]]; then
			echo "Target $TEST_TARGET for test $TEST_ID does not exist ... SKIPPING"
			return 0
		fi
	fi
	return 1
}

function run_all_tests()
{
	for i in $ALL_TESTS ; do
		TEST_ID=${i%:*:*:*:*}
		ENABLED=$(get_test_enabled $TEST_ID)
		TEST_COUNT=$(get_test_count $TEST_ID)
		TEST_TARGET=$(get_test_target $TEST_ID)

		if [[ $ENABLED -eq "1" ]]; then
			test_case $TEST_ID $TEST_COUNT $TEST_TARGET
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
	TEST_ID=$1
	NUM_TESTS=$2
	TARGET=$3

	if skip_test $TEST_ID $TARGET; then
		return
	fi

	i=0
	while [ $i -lt $NUM_TESTS ]; do
		test_num $TEST_ID
		watch_log $i ${TEST_NAME}_test_${TEST_ID} noclear
		RUN_TEST=${TEST_NAME}_test_${TEST_ID}
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
			test_case $1 $(get_test_count $1) $(get_test_target $1)
		elif [[ "$1" = "-c" ]]; then
			shift
			test_num $1
			test_num $2
			test_case $1 $2 $(get_test_target $1)
		elif [[ "$1" = "-s" ]]; then
			shift
			test_case $1 1 $(get_test_target $1)
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
check_production_sysctl_writes_strict
load_req_mod

trap "test_finish" EXIT

parse_args $@

exit 0
