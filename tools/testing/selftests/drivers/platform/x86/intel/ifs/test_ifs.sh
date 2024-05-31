#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test the functionality of the Intel IFS(In Field Scan) driver.
#

# Matched with kselftest framework: tools/testing/selftests/kselftest.h
readonly KSFT_PASS=0
readonly KSFT_FAIL=1
readonly KSFT_XFAIL=2
readonly KSFT_SKIP=4

readonly IFS_SCAN_MODE="0"
readonly IFS_PATH="/sys/devices/virtual/misc/intel_ifs"
readonly IFS_SCAN_SYSFS_PATH="${IFS_PATH}_${IFS_SCAN_MODE}"
readonly PASS="PASS"
readonly FAIL="FAIL"
readonly INFO="INFO"
readonly XFAIL="XFAIL"
readonly SKIP="SKIP"
readonly IFS_NAME="intel_ifs"

# Matches arch/x86/include/asm/intel-family.h and
# drivers/platform/x86/intel/ifs/core.c requirement as follows
readonly SAPPHIRERAPIDS_X="8f"
readonly EMERALDRAPIDS_X="cf"

readonly INTEL_FAM6="06"

FML=""
MODEL=""

TRUE="true"
FALSE="false"
RESULT=$KSFT_PASS
export INTERVAL_TIME=1
# For IFS cleanup tags
ORIGIN_IFS_LOADED=""
IFS_LOG="/tmp/ifs_logs.$$"

append_log()
{
	echo -e "$1" | tee -a "$IFS_LOG"
}

ifs_scan_result_summary()
{
	local failed_info pass_num skip_num fail_num

	if [[ -e "$IFS_LOG" ]]; then
		failed_info=$(grep ^"\[${FAIL}\]" "$IFS_LOG")
		fail_num=$(grep -c ^"\[${FAIL}\]" "$IFS_LOG")
		skip_num=$(grep -c ^"\[${SKIP}\]" "$IFS_LOG")
		pass_num=$(grep -c ^"\[${PASS}\]" "$IFS_LOG")

		if [[ "$fail_num" -ne 0 ]]; then
			RESULT=$KSFT_FAIL
			echo "[$INFO] IFS test failure summary:"
			echo "$failed_info"
		elif [[ "$skip_num" -ne 0 ]]; then
			RESULT=$KSFT_SKIP
		fi
			echo "[$INFO] IFS test pass:$pass_num, skip:$skip_num, fail:$fail_num"
	else
		echo "[$INFO] No file $IFS_LOG for IFS scan summary"
	fi
}

ifs_cleanup()
{
	lsmod | grep -q "$IFS_NAME" && [[ "$ORIGIN_IFS_LOADED" == "$FALSE" ]] && {
		echo "[$INFO] modprobe -r $IFS_NAME"
		modprobe -r "$IFS_NAME"
	}

	ifs_scan_result_summary
	[[ -e "$IFS_LOG" ]] && rm -rf "$IFS_LOG"

	echo "[RESULT] IFS test exit with $RESULT"
	exit "$RESULT"
}

test_exit()
{
	local info=$1
	RESULT=$2

	declare -A EXIT_MAP
	EXIT_MAP[$KSFT_PASS]=$PASS
	EXIT_MAP[$KSFT_FAIL]=$FAIL
	EXIT_MAP[$KSFT_XFAIL]=$XFAIL
	EXIT_MAP[$KSFT_SKIP]=$SKIP

	append_log "[${EXIT_MAP[$RESULT]}] $info"
	ifs_cleanup
}

get_cpu_fms()
{
	FML=$(grep -m 1 "family" /proc/cpuinfo | awk -F ":" '{printf "%02x",$2;}')
	MODEL=$(grep -m 1 "model" /proc/cpuinfo | awk -F ":" '{printf "%02x",$2;}')
}

check_cpu_ifs_support_interval_time()
{
	get_cpu_fms

	if [[ "$FML" != "$INTEL_FAM6" ]]; then
		test_exit "CPU family:$FML does not support IFS" "$KSFT_SKIP"
	fi

	# Ucode has time interval requirement for IFS scan on same CPU as follows:
	case $MODEL in
		"$SAPPHIRERAPIDS_X")
			INTERVAL_TIME=180;
			;;
		"$EMERALDRAPIDS_X")
			INTERVAL_TIME=30;
			;;
		*)
			# Set default interval time for other platforms
			INTERVAL_TIME=1;
			append_log "[$INFO] CPU FML:$FML model:0x$MODEL, default: 1s interval time"
			;;
	esac
}

check_ifs_loaded()
{
	local ifs_info=""

	ifs_info=$(lsmod | grep "$IFS_NAME")
	if [[ -z "$ifs_info" ]]; then
		append_log "[$INFO] modprobe $IFS_NAME"
		modprobe "$IFS_NAME" || {
			test_exit "Check if CONFIG_INTEL_IFS is set to m or \
platform doesn't support ifs" "$KSFT_SKIP"
		}
		ifs_info=$(lsmod | grep "$IFS_NAME")
		[[ -n "$ifs_info" ]] || test_exit "No ifs module listed by lsmod" "$KSFT_FAIL"
	fi
}

test_ifs_scan_entry()
{
	local ifs_info=""

	ifs_info=$(lsmod | grep "$IFS_NAME")

	if [[ -z "$ifs_info" ]]; then
		ORIGIN_IFS_LOADED="$FALSE"
		check_ifs_loaded
	else
		ORIGIN_IFS_LOADED="$TRUE"
		append_log "[$INFO] Module $IFS_NAME is already loaded"
	fi

	if [[ -d "$IFS_SCAN_SYSFS_PATH" ]]; then
		append_log "[$PASS] IFS sysfs $IFS_SCAN_SYSFS_PATH entry is created\n"
	else
		test_exit "No sysfs entry in $IFS_SCAN_SYSFS_PATH" "$KSFT_FAIL"
	fi
}

prepare_ifs_test_env()
{
	check_cpu_ifs_support_interval_time
}

test_ifs()
{
	prepare_ifs_test_env

	test_ifs_scan_entry
}

trap ifs_cleanup SIGTERM SIGINT
test_ifs
ifs_cleanup
