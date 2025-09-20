#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="null_01"
ERR_CODE=0

_prep_test "null" "basic IO test"

dev_id=$(_add_ublk_dev -t null)
_check_add_dev $TID $?

# run fio over the two disks
fio --name=job1 --filename=/dev/ublkb"${dev_id}" --ioengine=libaio --rw=readwrite --iodepth=32 --size=256M > /dev/null 2>&1
ERR_CODE=$?

_cleanup_test "null"

_show_result $TID $ERR_CODE
