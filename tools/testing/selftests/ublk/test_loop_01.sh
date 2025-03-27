#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="loop_01"
ERR_CODE=0

_prep_test "loop" "write and verify test"

backfile_0=$(_create_backfile 256M)

dev_id=$(_add_ublk_dev -t loop "$backfile_0")
_check_add_dev $TID $? "${backfile_0}"

# run fio over the ublk disk
fio --name=write_and_verify \
    --filename=/dev/ublkb"${dev_id}" \
    --ioengine=libaio --iodepth=16 \
    --rw=write \
    --size=256M \
    --direct=1 \
    --verify=crc32c \
    --do_verify=1 \
    --bs=4k > /dev/null 2>&1
ERR_CODE=$?

_cleanup_test "loop"

_remove_backfile "$backfile_0"

_show_result $TID $ERR_CODE
