#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Runs a set of tests in a given subdirectory.
. $(dirname "$(readlink -e "${BASH_SOURCE[0]}")")/ktap_helpers.sh
export timeout_rc=124
export logfile=/dev/stdout
export per_test_logging=
export per_test_log_dir=/tmp
export RUN_IN_NETNS=

# Defaults for "settings" file fields:
# "timeout" how many seconds to let each test run before running
# over our soft timeout limit.
export kselftest_default_timeout=45

# There isn't a shell-agnostic way to find the path of a sourced file,
# so we must rely on BASE_DIR being set to find other tools.
if [ -z "$BASE_DIR" ]; then
	echo "Error: BASE_DIR must be set before sourcing." >&2
	exit 1
fi

TR_CMD=$(command -v tr)

# If Perl is unavailable, we must fall back to line-at-a-time prefixing
# with sed instead of unbuffered output.
tap_prefix()
{
	if [ ! -x /usr/bin/perl ]; then
		sed -e 's/^/# /'
	else
		"$BASE_DIR"/kselftest/prefix.pl
	fi
}

tap_timeout()
{
	# Make sure tests will time out if utility is available.
	if [ -x /usr/bin/timeout ] ; then
		/usr/bin/timeout --foreground "$kselftest_timeout" \
			/usr/bin/timeout "$kselftest_timeout" $1
	else
		$1
	fi
}

run_one()
{
	DIR="$1"
	TEST="$2"
	local rc test_num="$3"

	BASENAME_TEST=$(basename $TEST)

	# Reset any "settings"-file variables.
	export kselftest_timeout="$kselftest_default_timeout"

	# Safe default if tr not available
	kselftest_cmd_args_ref="KSELFTEST_ARGS"

	# Optional arguments for this command, possibly defined as an
	# environment variable built using the test executable in all
	# uppercase and sanitized substituting non acceptable shell
	# variable name characters with "_" as in:
	#
	# 	KSELFTEST_<UPPERCASE_SANITIZED_TESTNAME>_ARGS="<options>"
	#
	# e.g.
	#
	# 	rtctest --> KSELFTEST_RTCTEST_ARGS="/dev/rtc1"
	#
	# 	cpu-on-off-test.sh --> KSELFTEST_CPU_ON_OFF_TEST_SH_ARGS="-a -p 10"
	#
	if [ -n "$TR_CMD" ]; then
		BASENAME_SANITIZED=$(echo "$BASENAME_TEST" | \
					$TR_CMD -d "[:blank:][:cntrl:]" | \
					$TR_CMD -c "[:alnum:]_" "_" | \
					$TR_CMD [:lower:] [:upper:])
		kselftest_cmd_args_ref="KSELFTEST_${BASENAME_SANITIZED}_ARGS"
	fi

	# Load per-test-directory kselftest "settings" file.
	settings="$BASE_DIR/$DIR/settings"
	if [ -r "$settings" ] ; then
		while read line ; do
			# Skip comments.
			if echo "$line" | grep -q '^#'; then
				continue
			fi
			field=$(echo "$line" | cut -d= -f1)
			value=$(echo "$line" | cut -d= -f2-)
			eval "kselftest_$field"="$value"
		done < "$settings"
	fi

	# Command line timeout overrides the settings file
	if [ -n "$kselftest_override_timeout" ]; then
		kselftest_timeout="$kselftest_override_timeout"
		ktap_print_msg "overriding timeout to $kselftest_timeout" >> "$logfile"
	else
		ktap_print_msg "timeout set to $kselftest_timeout" >> "$logfile"
	fi

	TEST_HDR_MSG="selftests: $DIR: $BASENAME_TEST"
	echo "# $TEST_HDR_MSG"
	if [ ! -e "$TEST" ]; then
		ktap_print_msg "Warning: file $TEST is missing!"
		ktap_test_fail "$test_num $TEST_HDR_MSG"
		rc=$KSFT_FAIL
	else
		if [ -x /usr/bin/stdbuf ]; then
			stdbuf="/usr/bin/stdbuf --output=L "
		fi
		eval kselftest_cmd_args="\$${kselftest_cmd_args_ref:-}"
		if [ -x "$TEST" ]; then
			cmd="$stdbuf ./$BASENAME_TEST $kselftest_cmd_args"
		elif [ -x "./ksft_runner.sh" ]; then
			cmd="$stdbuf ./ksft_runner.sh ./$BASENAME_TEST"
		else
			ktap_print_msg "Warning: file $TEST is not executable"

			if [ $(head -n 1 "$TEST" | cut -c -2) = "#!" ]
			then
				interpreter=$(head -n 1 "$TEST" | cut -c 3-)
				cmd="$stdbuf $interpreter ./$BASENAME_TEST"
			else
				ktap_test_fail "$test_num $TEST_HDR_MSG"
				return $KSFT_FAIL
			fi
		fi
		cd `dirname $TEST` > /dev/null
		(((( tap_timeout "$cmd" 2>&1; echo $? >&3) |
			tap_prefix >&4) 3>&1) |
			(read xs; exit $xs)) 4>>"$logfile"
		rc=$?
		case "$rc" in
		"$KSFT_PASS")
			ktap_test_pass "$test_num $TEST_HDR_MSG";;
		"$KSFT_SKIP")
			ktap_test_skip "$test_num $TEST_HDR_MSG";;
		"$KSFT_XFAIL")
			ktap_test_xfail "$test_num $TEST_HDR_MSG";;
		"$timeout_rc")
			ktap_test_fail "$test_num $TEST_HDR_MSG # TIMEOUT $kselftest_timeout seconds";;
		*)
			ktap_test_fail "$test_num $TEST_HDR_MSG # exit=$rc";;
		esac
		cd - >/dev/null
	fi

	return $rc
}

in_netns()
{
	local name=$1
	ip netns exec $name bash <<-EOF
		BASE_DIR=$BASE_DIR
		source $BASE_DIR/kselftest/runner.sh
		logfile=$logfile
		run_one $DIR $TEST $test_num
	EOF
}

run_in_netns()
{
	local tmplog="/tmp/$(mktemp -u ${BASENAME_TEST}-XXXXXX)"
	local netns=$(mktemp -u ${BASENAME_TEST}-XXXXXX)
	local rc

	ip netns add $netns
	if [ $? -ne 0 ]; then
		ktap_print_msg "Warning: Create namespace failed for $BASENAME_TEST"
		ktap_test_fail "$test_num selftests: $DIR: $BASENAME_TEST # Create NS failed"
	fi
	ip -n $netns link set lo up

	in_netns $netns &> $tmplog
	rc=$?

	ip netns del $netns &> /dev/null
	# Cat the log at once to avoid parallel netns logs.
	cat $tmplog
	rm -f $tmplog
	return $rc
}

run_many()
{
	DIR="${PWD#${BASE_DIR}/}"
	test_num=0
	local rc
	pids=()

	for TEST in "$@"; do
		BASENAME_TEST=$(basename $TEST)
		test_num=$(( test_num + 1 ))
		if [ -n "$per_test_logging" ]; then
			logfile="$per_test_log_dir/$BASENAME_TEST"
			cat /dev/null > "$logfile"
		fi
		if [ -n "$RUN_IN_NETNS" ]; then
			run_in_netns &
			pids+=($!)
		else
			run_one "$DIR" "$TEST" "$test_num"
		fi
	done

	# These variables are outputs of ktap_helpers.sh but since we've
	# run the test in a subprocess we need to update them manually
	for pid in "${pids[@]}"; do
		wait "$pid"
		rc=$?
		case "$rc" in
		"$KSFT_PASS")
			KTAP_CNT_PASS=$((KTAP_CNT_PASS + 1));;
		"$KSFT_FAIL")
			KTAP_CNT_FAIL=$((KTAP_CNT_FAIL + 1));;
		"$KSFT_SKIP")
			KTAP_CNT_SKIP=$((KTAP_CNT_SKIP + 1));;
		"$KSFT_XFAIL")
			KTAP_CNT_XFAIL=$((KTAP_CNT_XFAIL + 1));;
		*)
			KTAP_CNT_FAIL=$((KTAP_CNT_FAIL + 1));;
		esac
	done
}
