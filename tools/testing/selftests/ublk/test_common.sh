#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

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
	echo "ublk $type: $@"
}

_show_result()
{
	if [ $2 -ne 0 ]; then
		echo "$1 : [FAIL]"
	else
		echo "$1 : [PASS]"
	fi
}

_cleanup_test() {
	${UBLK_PROG} del -n $1
}

_add_ublk_dev() {
	local kublk_temp=`mktemp /tmp/kublk-XXXXXX`
	${UBLK_PROG} add $@ > ${kublk_temp} 2>&1
	if [ $? -ne 0 ]; then
		echo "fail to add ublk dev $@"
		exit -1
	fi
	local dev_id=`grep "dev id" ${kublk_temp} | awk -F '[ :]' '{print $3}'`
	udevadm settle
	rm -f ${kublk_temp}
	echo ${dev_id}
}

export UBLK_PROG=$(pwd)/kublk
