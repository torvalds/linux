#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

if ! _have_feature "BATCH_IO"; then
	exit "$UBLK_SKIP_CODE"
fi

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "generic" "test UBLK_F_BATCH_IO with 4_threads vs. 1_queues"

_create_backfile 0 512M

dev_id=$(_add_ublk_dev -t loop -q 1 --nthreads 4 -b "${UBLK_BACKFILES[0]}")
_check_add_dev $TID $?

# run fio over the ublk disk
fio --name=job1 --filename=/dev/ublkb"${dev_id}" --ioengine=libaio --rw=readwrite \
	--iodepth=32 --size=100M --numjobs=4 > /dev/null 2>&1
ERR_CODE=$?

_cleanup_test "generic"
_show_result $TID $ERR_CODE
