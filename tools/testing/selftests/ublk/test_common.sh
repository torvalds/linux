#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

_create_backfile() {
	local my_size=$1
	local my_file

	my_file=$(mktemp ublk_file_"${my_size}"_XXXXX)
	truncate -s "${my_size}" "${my_file}"
	echo "$my_file"
}

_remove_backfile() {
	local file=$1

	[ -f "$file" ] && rm -f "$file"
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
	echo "ublk $type: $*"
}

_show_result()
{
	if [ "$2" -ne 0 ]; then
		echo "$1 : [FAIL]"
	else
		echo "$1 : [PASS]"
	fi
}

_cleanup_test() {
	"${UBLK_PROG}" del -a
}

_add_ublk_dev() {
	local kublk_temp;
	local dev_id;

	kublk_temp=$(mktemp /tmp/kublk-XXXXXX)
	if ! "${UBLK_PROG}" add "$@" > "${kublk_temp}" 2>&1; then
		echo "fail to add ublk dev $*"
		return 255
	fi

	dev_id=$(grep "dev id" "${kublk_temp}" | awk -F '[ :]' '{print $3}')
	udevadm settle
	rm -f "${kublk_temp}"
	echo "${dev_id}"
}

_have_feature()
{
	if  "$UBLK_PROG" "features" | grep "$1" > /dev/null 2>&1; then
		return 0
	fi
	return 1
}

UBLK_PROG=$(pwd)/kublk
export UBLK_PROG
