#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

UBLK_SKIP_CODE=4

_have_program() {
	if command -v "$1" >/dev/null 2>&1; then
		return 0
	fi
	return 1
}

_get_disk_dev_t() {
	local dev_id=$1
	local dev
	local major
	local minor

	dev=/dev/ublkb"${dev_id}"
	major="0x"$(stat -c '%t' "$dev")
	minor="0x"$(stat -c '%T' "$dev")

	echo $(( (major & 0xfff) << 20 | (minor & 0xfffff) ))
}

_run_fio_verify_io() {
	fio --name=verify --rw=randwrite --direct=1 --ioengine=libaio \
		--bs=8k --iodepth=32 --verify=crc32c --do_verify=1 \
		--verify_state_save=0 "$@" > /dev/null
}

_create_backfile() {
	local index=$1
	local new_size=$2
	local old_file
	local new_file

	old_file="${UBLK_BACKFILES[$index]}"
	[ -f "$old_file" ] && rm -f "$old_file"

	new_file=$(mktemp ublk_file_"${new_size}"_XXXXX)
	truncate -s "${new_size}" "${new_file}"
	UBLK_BACKFILES["$index"]="$new_file"
}

_remove_files() {
	local file

	for file in "${UBLK_BACKFILES[@]}"; do
		[ -f "$file" ] && rm -f "$file"
	done
	[ -f "$UBLK_TMP" ] && rm -f "$UBLK_TMP"
}

_create_tmp_dir() {
	local my_file;

	my_file=$(mktemp -d ublk_dir_XXXXX)
	echo "$my_file"
}

_remove_tmp_dir() {
	local dir=$1

	[ -d "$dir" ] && rmdir "$dir"
}

_mkfs_mount_test()
{
	local dev=$1
	local err_code=0
	local mnt_dir;

	mnt_dir=$(_create_tmp_dir)
	mkfs.ext4 -F "$dev" > /dev/null 2>&1
	err_code=$?
	if [ $err_code -ne 0 ]; then
		return $err_code
	fi

	mount -t ext4 "$dev" "$mnt_dir" > /dev/null 2>&1
	umount "$dev"
	err_code=$?
	_remove_tmp_dir "$mnt_dir"
	if [ $err_code -ne 0 ]; then
		return $err_code
	fi
}

_check_root() {
	local ksft_skip=4

	if [ $UID != 0 ]; then
		echo please run this as root >&2
		exit $ksft_skip
	fi
}

_remove_ublk_devices() {
	${UBLK_PROG} del -a
	modprobe -r ublk_drv > /dev/null 2>&1
}

_get_ublk_dev_state() {
	${UBLK_PROG} list -n "$1" | grep "state" | awk '{print $11}'
}

_get_ublk_daemon_pid() {
	${UBLK_PROG} list -n "$1" | grep "pid" | awk '{print $7}'
}

_prep_test() {
	_check_root
	local type=$1
	shift 1
	modprobe ublk_drv > /dev/null 2>&1
	UBLK_TMP=$(mktemp ublk_test_XXXXX)
	[ "$UBLK_TEST_QUIET" -eq 0 ] && echo "ublk $type: $*"
}

_remove_test_files()
{
	local files=$*

	for file in ${files}; do
		[ -f "${file}" ] && rm -f "${file}"
	done
}

_show_result()
{
	if [ "$UBLK_TEST_SHOW_RESULT" -ne 0 ]; then
		if [ "$2" -eq 0 ]; then
			echo "$1 : [PASS]"
		elif [ "$2" -eq 4 ]; then
			echo "$1 : [SKIP]"
		else
			echo "$1 : [FAIL]"
		fi
	fi
	if [ "$2" -ne 0 ]; then
		_remove_files
		exit "$2"
	fi
	return 0
}

# don't call from sub-shell, otherwise can't exit
_check_add_dev()
{
	local tid=$1
	local code=$2

	if [ "${code}" -ne 0 ]; then
		_show_result "${tid}" "${code}"
	fi
}

_cleanup_test() {
	"${UBLK_PROG}" del -a

	_remove_files
}

_have_feature()
{
	if  $UBLK_PROG "features" | grep "$1" > /dev/null 2>&1; then
		return 0
	fi
	return 1
}

_create_ublk_dev() {
	local dev_id;
	local cmd=$1

	shift 1

	if [ ! -c /dev/ublk-control ]; then
		return ${UBLK_SKIP_CODE}
	fi
	if echo "$@" | grep -q "\-z"; then
		if ! _have_feature "ZERO_COPY"; then
			return ${UBLK_SKIP_CODE}
		fi
	fi

	if ! dev_id=$("${UBLK_PROG}" "$cmd" "$@" | grep "dev id" | awk -F '[ :]' '{print $3}'); then
		echo "fail to add ublk dev $*"
		return 255
	fi
	udevadm settle

	if [[ "$dev_id" =~ ^[0-9]+$ ]]; then
		echo "${dev_id}"
	else
		return 255
	fi
}

_add_ublk_dev() {
	_create_ublk_dev "add" "$@"
}

_recover_ublk_dev() {
	local dev_id
	local state

	dev_id=$(_create_ublk_dev "recover" "$@")
	for ((j=0;j<20;j++)); do
		state=$(_get_ublk_dev_state "${dev_id}")
		[ "$state" == "LIVE" ] && break
		sleep 1
	done
	echo "$state"
}

# kill the ublk daemon and return ublk device state
__ublk_kill_daemon()
{
	local dev_id=$1
	local exp_state=$2
	local daemon_pid
	local state

	daemon_pid=$(_get_ublk_daemon_pid "${dev_id}")
	state=$(_get_ublk_dev_state "${dev_id}")

	for ((j=0;j<50;j++)); do
		[ "$state" == "$exp_state" ] && break
		kill -9 "$daemon_pid" > /dev/null 2>&1
		sleep 1
		state=$(_get_ublk_dev_state "${dev_id}")
	done
	echo "$state"
}

__remove_ublk_dev_return() {
	local dev_id=$1

	${UBLK_PROG} del -n "${dev_id}"
	local res=$?
	udevadm settle
	return ${res}
}

__run_io_and_remove()
{
	local dev_id=$1
	local size=$2
	local kill_server=$3

	fio --name=job1 --filename=/dev/ublkb"${dev_id}" --ioengine=libaio \
		--rw=readwrite --iodepth=256 --size="${size}" --numjobs=4 \
		--runtime=20 --time_based > /dev/null 2>&1 &
	sleep 2
	if [ "${kill_server}" = "yes" ]; then
		local state
		state=$(__ublk_kill_daemon "${dev_id}" "DEAD")
		if [ "$state" != "DEAD" ]; then
			echo "device isn't dead($state) after killing daemon"
			return 255
		fi
	fi
	if ! __remove_ublk_dev_return "${dev_id}"; then
		echo "delete dev ${dev_id} failed"
		return 255
	fi
	wait
}

run_io_and_remove()
{
	local size=$1
	local dev_id
	shift 1

	dev_id=$(_add_ublk_dev "$@")
	_check_add_dev "$TID" $?

	[ "$UBLK_TEST_QUIET" -eq 0 ] && echo "run ublk IO vs. remove device(ublk add $*)"
	if ! __run_io_and_remove "$dev_id" "${size}" "no"; then
		echo "/dev/ublkc$dev_id isn't removed"
		exit 255
	fi
}

run_io_and_kill_daemon()
{
	local size=$1
	local dev_id
	shift 1

	dev_id=$(_add_ublk_dev "$@")
	_check_add_dev "$TID" $?

	[ "$UBLK_TEST_QUIET" -eq 0 ] && echo "run ublk IO vs kill ublk server(ublk add $*)"
	if ! __run_io_and_remove "$dev_id" "${size}" "yes"; then
		echo "/dev/ublkc$dev_id isn't removed res ${res}"
		exit 255
	fi
}

run_io_and_recover()
{
	local state
	local dev_id

	dev_id=$(_add_ublk_dev "$@")
	_check_add_dev "$TID" $?

	fio --name=job1 --filename=/dev/ublkb"${dev_id}" --ioengine=libaio \
		--rw=readwrite --iodepth=256 --size="${size}" --numjobs=4 \
		--runtime=20 --time_based > /dev/null 2>&1 &
	sleep 4

	state=$(__ublk_kill_daemon "${dev_id}" "QUIESCED")
	if [ "$state" != "QUIESCED" ]; then
		echo "device isn't quiesced($state) after killing daemon"
		return 255
	fi

	state=$(_recover_ublk_dev -n "$dev_id" "$@")
	if [ "$state" != "LIVE" ]; then
		echo "faile to recover to LIVE($state)"
		return 255
	fi

	if ! __remove_ublk_dev_return "${dev_id}"; then
		echo "delete dev ${dev_id} failed"
		return 255
	fi
	wait
}


_ublk_test_top_dir()
{
	cd "$(dirname "$0")" && pwd
}

UBLK_PROG=$(_ublk_test_top_dir)/kublk
UBLK_TEST_QUIET=1
UBLK_TEST_SHOW_RESULT=1
UBLK_BACKFILES=()
export UBLK_PROG
export UBLK_TEST_QUIET
export UBLK_TEST_SHOW_RESULT
