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

readonly CPU_SYSFS="/sys/devices/system/cpu"
readonly CPU_OFFLINE_SYSFS="${CPU_SYSFS}/offline"
readonly IMG_PATH="/lib/firmware/intel/ifs_0"
readonly IFS_SCAN_MODE="0"
readonly IFS_ARRAY_BIST_SCAN_MODE="1"
readonly IFS_PATH="/sys/devices/virtual/misc/intel_ifs"
readonly IFS_SCAN_SYSFS_PATH="${IFS_PATH}_${IFS_SCAN_MODE}"
readonly IFS_ARRAY_BIST_SYSFS_PATH="${IFS_PATH}_${IFS_ARRAY_BIST_SCAN_MODE}"
readonly RUN_TEST="run_test"
readonly STATUS="status"
readonly DETAILS="details"
readonly STATUS_PASS="pass"
readonly PASS="PASS"
readonly FAIL="FAIL"
readonly INFO="INFO"
readonly XFAIL="XFAIL"
readonly SKIP="SKIP"
readonly IFS_NAME="intel_ifs"
readonly ALL="all"
readonly SIBLINGS="siblings"

# Matches arch/x86/include/asm/intel-family.h and
# drivers/platform/x86/intel/ifs/core.c requirement as follows
readonly SAPPHIRERAPIDS_X="8f"
readonly EMERALDRAPIDS_X="cf"

readonly INTEL_FAM6="06"

LOOP_TIMES=3
FML=""
MODEL=""
STEPPING=""
CPU_FMS=""
TRUE="true"
FALSE="false"
RESULT=$KSFT_PASS
IMAGE_NAME=""
INTERVAL_TIME=1
OFFLINE_CPUS=""
# For IFS cleanup tags
ORIGIN_IFS_LOADED=""
IFS_IMAGE_NEED_RESTORE=$FALSE
IFS_LOG="/tmp/ifs_logs.$$"
RANDOM_CPU=""
DEFAULT_IMG_ID=""

append_log()
{
	echo -e "$1" | tee -a "$IFS_LOG"
}

online_offline_cpu_list()
{
	local on_off=$1
	local target_cpus=$2
	local cpu=""
	local cpu_start=""
	local cpu_end=""
	local i=""

	if [[ -n "$target_cpus" ]]; then
		for cpu in $(echo "$target_cpus" | tr ',' ' '); do
			if [[ "$cpu" == *"-"* ]]; then
				cpu_start=""
				cpu_end=""
				i=""
				cpu_start=$(echo "$cpu" | cut -d "-" -f 1)
				cpu_end=$(echo "$cpu" | cut -d "-" -f 2)
				for((i=cpu_start;i<=cpu_end;i++)); do
					append_log "[$INFO] echo $on_off > \
${CPU_SYSFS}/cpu${i}/online"
					echo "$on_off" > "$CPU_SYSFS"/cpu"$i"/online
				done
			else
				set_target_cpu "$on_off" "$cpu"
			fi
		done
	fi
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
	echo "[$INFO] Restore environment after IFS test"

	# Restore ifs origin image if origin image backup step is needed
	[[ "$IFS_IMAGE_NEED_RESTORE" == "$TRUE" ]] && {
		mv -f "$IMG_PATH"/"$IMAGE_NAME"_origin "$IMG_PATH"/"$IMAGE_NAME"
	}

	# Restore the CPUs to the state before testing
	[[ -z "$OFFLINE_CPUS" ]] || online_offline_cpu_list "0" "$OFFLINE_CPUS"

	lsmod | grep -q "$IFS_NAME" && [[ "$ORIGIN_IFS_LOADED" == "$FALSE" ]] && {
		echo "[$INFO] modprobe -r $IFS_NAME"
		modprobe -r "$IFS_NAME"
	}

	ifs_scan_result_summary
	[[ -e "$IFS_LOG" ]] && rm -rf "$IFS_LOG"

	echo "[RESULT] IFS test exit with $RESULT"
	exit "$RESULT"
}

do_cmd()
{
	local cmd=$*
	local ret=""

	append_log "[$INFO] $cmd"
	eval "$cmd"
	ret=$?
	if [[ $ret -ne 0 ]]; then
		append_log "[$FAIL] $cmd failed. Return code is $ret"
		RESULT=$KSFT_XFAIL
		ifs_cleanup
	fi
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

online_all_cpus()
{
	local off_cpus=""

	OFFLINE_CPUS=$(cat "$CPU_OFFLINE_SYSFS")
	online_offline_cpu_list "1" "$OFFLINE_CPUS"

	off_cpus=$(cat "$CPU_OFFLINE_SYSFS")
	if [[ -z "$off_cpus" ]]; then
		append_log "[$INFO] All CPUs are online."
	else
		append_log "[$XFAIL] There is offline cpu:$off_cpus after online all cpu!"
		RESULT=$KSFT_XFAIL
		ifs_cleanup
	fi
}

get_cpu_fms()
{
	FML=$(grep -m 1 "family" /proc/cpuinfo | awk -F ":" '{printf "%02x",$2;}')
	MODEL=$(grep -m 1 "model" /proc/cpuinfo | awk -F ":" '{printf "%02x",$2;}')
	STEPPING=$(grep -m 1 "stepping" /proc/cpuinfo | awk -F ":" '{printf "%02x",$2;}')
	CPU_FMS="${FML}-${MODEL}-${STEPPING}"
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

load_image()
{
	local image_id=$1
	local image_info=""
	local ret=""

	check_ifs_loaded
	if [[ -e "${IMG_PATH}/${IMAGE_NAME}" ]]; then
		append_log "[$INFO] echo 0x$image_id > ${IFS_SCAN_SYSFS_PATH}/current_batch"
		echo "0x$image_id" > "$IFS_SCAN_SYSFS_PATH"/current_batch 2>/dev/null
		ret=$?
		[[ "$ret" -eq 0 ]] || {
			append_log "[$FAIL] Load ifs image $image_id failed with ret:$ret\n"
			return "$ret"
		}
		image_info=$(cat ${IFS_SCAN_SYSFS_PATH}/current_batch)
		if [[ "$image_info" == 0x"$image_id" ]]; then
			append_log "[$PASS] load IFS current_batch:$image_info"
		else
			append_log "[$FAIL] current_batch:$image_info is not expected:$image_id"
			return "$KSFT_FAIL"
		fi
	else
		append_log "[$FAIL] No IFS image file ${IMG_PATH}/${IMAGE_NAME}"\
		return "$KSFT_FAIL"
	fi
	return 0
}

test_load_origin_ifs_image()
{
	local image_id=$1

	IMAGE_NAME="${CPU_FMS}-${image_id}.scan"

	load_image "$image_id" || return $?
	return 0
}

test_load_bad_ifs_image()
{
	local image_id=$1

	IMAGE_NAME="${CPU_FMS}-${image_id}.scan"

	do_cmd "mv -f ${IMG_PATH}/${IMAGE_NAME} ${IMG_PATH}/${IMAGE_NAME}_origin"

	# Set IFS_IMAGE_NEED_RESTORE to true before corrupt the origin ifs image file
	IFS_IMAGE_NEED_RESTORE=$TRUE
	do_cmd "dd if=/dev/urandom of=${IMG_PATH}/${IMAGE_NAME} bs=1K count=6 2>/dev/null"

	# Use the specified judgment for negative testing
	append_log "[$INFO] echo 0x$image_id > ${IFS_SCAN_SYSFS_PATH}/current_batch"
	echo "0x$image_id" > "$IFS_SCAN_SYSFS_PATH"/current_batch 2>/dev/null
	ret=$?
	if [[ "$ret" -ne 0 ]]; then
		append_log "[$PASS] Load invalid ifs image failed with ret:$ret not 0 as expected"
	else
		append_log "[$FAIL] Load invalid ifs image ret:$ret unexpectedly"
	fi

	do_cmd "mv -f ${IMG_PATH}/${IMAGE_NAME}_origin ${IMG_PATH}/${IMAGE_NAME}"
	IFS_IMAGE_NEED_RESTORE=$FALSE
}

test_bad_and_origin_ifs_image()
{
	local image_id=$1

	append_log "[$INFO] Test loading bad and then loading original IFS image:"
	test_load_origin_ifs_image "$image_id" || return $?
	test_load_bad_ifs_image "$image_id"
	# Load origin image again and make sure it's worked
	test_load_origin_ifs_image "$image_id" || return $?
	append_log "[$INFO] Loading invalid IFS image and then loading initial image passed.\n"
}

ifs_test_cpu()
{
	local ifs_mode=$1
	local cpu_num=$2
	local image_id status details ret result result_info

	echo "$cpu_num" > "$IFS_PATH"_"$ifs_mode"/"$RUN_TEST"
	ret=$?

	status=$(cat "${IFS_PATH}_${ifs_mode}/${STATUS}")
	details=$(cat "${IFS_PATH}_${ifs_mode}/${DETAILS}")

	if [[ "$ret" -eq 0 && "$status" == "$STATUS_PASS" ]]; then
		result="$PASS"
	else
		result="$FAIL"
	fi

	cpu_num=$(cat "${CPU_SYSFS}/cpu${cpu_num}/topology/thread_siblings_list")

	# There is no image file for IFS ARRAY BIST scan
	if [[ -e "${IFS_PATH}_${ifs_mode}/current_batch" ]]; then
		image_id=$(cat "${IFS_PATH}_${ifs_mode}/current_batch")
		result_info=$(printf "[%s] ifs_%1d cpu(s):%s, current_batch:0x%02x, \
ret:%2d, status:%s, details:0x%016x" \
			     "$result" "$ifs_mode" "$cpu_num" "$image_id" "$ret" \
			     "$status" "$details")
	else
		result_info=$(printf "[%s] ifs_%1d cpu(s):%s, ret:%2d, status:%s, details:0x%016x" \
			     "$result" "$ifs_mode" "$cpu_num" "$ret" "$status" "$details")
	fi

	append_log "$result_info"
}

ifs_test_cpus()
{
	local cpus_type=$1
	local ifs_mode=$2
	local image_id=$3
	local cpu_max_num=""
	local cpu_num=""

	case "$cpus_type" in
		"$ALL")
			cpu_max_num=$(($(nproc) - 1))
			cpus=$(seq 0 $cpu_max_num)
			;;
		"$SIBLINGS")
			cpus=$(cat ${CPU_SYSFS}/cpu*/topology/thread_siblings_list \
				| sed -e 's/,.*//' \
				| sed -e 's/-.*//' \
				| sort -n \
				| uniq)
			;;
		*)
			test_exit "Invalid cpus_type:$cpus_type" "$KSFT_XFAIL"
			;;
	esac

	for cpu_num in $cpus; do
		ifs_test_cpu "$ifs_mode" "$cpu_num"
	done

	if [[ -z "$image_id" ]]; then
		append_log "[$INFO] ifs_$ifs_mode test $cpus_type cpus completed\n"
	else
		append_log "[$INFO] ifs_$ifs_mode $cpus_type cpus with $CPU_FMS-$image_id.scan \
completed\n"
	fi
}

test_ifs_same_cpu_loop()
{
	local ifs_mode=$1
	local cpu_num=$2
	local loop_times=$3

	append_log "[$INFO] Test ifs mode $ifs_mode on CPU:$cpu_num for $loop_times rounds:"
	[[ "$ifs_mode" == "$IFS_SCAN_MODE" ]] && {
		load_image "$DEFAULT_IMG_ID" ||	return $?
	}
	for (( i=1; i<=loop_times; i++ )); do
		append_log "[$INFO] Loop iteration: $i in total of $loop_times"
		# Only IFS scan needs the interval time
		if [[ "$ifs_mode" == "$IFS_SCAN_MODE" ]]; then
			do_cmd "sleep $INTERVAL_TIME"
		elif [[ "$ifs_mode" == "$IFS_ARRAY_BIST_SCAN_MODE" ]]; then
			true
		else
			test_exit "Invalid ifs_mode:$ifs_mode" "$KSFT_XFAIL"
		fi

		ifs_test_cpu "$ifs_mode" "$cpu_num"
	done
	append_log "[$INFO] $loop_times rounds of ifs_$ifs_mode test on CPU:$cpu_num completed.\n"
}

test_ifs_scan_available_imgs()
{
	local image_ids=""
	local image_id=""

	append_log "[$INFO] Test ifs scan with available images:"
	image_ids=$(find "$IMG_PATH" -maxdepth 1 -name "${CPU_FMS}-[0-9a-fA-F][0-9a-fA-F].scan" \
		    2>/dev/null \
		    | sort \
		    | awk -F "-" '{print $NF}' \
		    | cut -d "." -f 1)

	for image_id in $image_ids; do
		load_image "$image_id" || return $?

		ifs_test_cpus "$SIBLINGS" "$IFS_SCAN_MODE" "$image_id"
		# IFS scan requires time interval for the scan on the same CPU
		do_cmd "sleep $INTERVAL_TIME"
	done
}

prepare_ifs_test_env()
{
	local max_cpu=""

	check_cpu_ifs_support_interval_time

	online_all_cpus
	max_cpu=$(($(nproc) - 1))
	RANDOM_CPU=$(shuf -i 0-$max_cpu -n 1)

	DEFAULT_IMG_ID=$(find $IMG_PATH -maxdepth 1 -name "${CPU_FMS}-[0-9a-fA-F][0-9a-fA-F].scan" \
			 2>/dev/null \
			 | sort \
			 | head -n 1 \
			 | awk -F "-" '{print $NF}' \
			 | cut -d "." -f 1)
}

test_ifs()
{
	prepare_ifs_test_env

	test_ifs_scan_entry

	if [[ -z "$DEFAULT_IMG_ID" ]]; then
		append_log "[$SKIP] No proper ${IMG_PATH}/${CPU_FMS}-*.scan, skip ifs_0 scan"
	else
		test_bad_and_origin_ifs_image "$DEFAULT_IMG_ID"
		test_ifs_scan_available_imgs
		test_ifs_same_cpu_loop "$IFS_SCAN_MODE" "$RANDOM_CPU" "$LOOP_TIMES"
	fi

	if [[ -d "$IFS_ARRAY_BIST_SYSFS_PATH" ]]; then
		ifs_test_cpus "$SIBLINGS" "$IFS_ARRAY_BIST_SCAN_MODE"
		test_ifs_same_cpu_loop "$IFS_ARRAY_BIST_SCAN_MODE" "$RANDOM_CPU" "$LOOP_TIMES"
	else
		append_log "[$SKIP] No $IFS_ARRAY_BIST_SYSFS_PATH, skip IFS ARRAY BIST scan"
	fi
}

trap ifs_cleanup SIGTERM SIGINT
test_ifs
ifs_cleanup
