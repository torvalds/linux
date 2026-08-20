#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# --rotate_auto_buf: COMMIT must unregister old auto_buf index before store.

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

if ! _have_feature "BATCH_IO" || ! _have_feature "AUTO_BUF_REG"; then
	exit "$UBLK_SKIP_CODE"
fi
if ! _have_program fio || ! _have_program timeout; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "generic" "batch auto_buf unregister with rotating index"

_create_backfile 0 64M

dev_id=$(_add_ublk_dev_no_settle -t loop -q 1 --nthreads 1 -b --auto_zc \
	--rotate_auto_buf "${UBLK_BACKFILES[0]}")
_check_add_dev $TID $?

for ((i = 0; i < 50; i++)); do
	[ -b /dev/ublkb"${dev_id}" ] && break
	sleep 0.1
done
[ -b /dev/ublkb"${dev_id}" ] || { _cleanup_test; _show_result $TID 1; }

timeout -k 2 5 fio --name=job1 --filename=/dev/ublkb"${dev_id}" \
	--ioengine=libaio --rw=write --direct=1 --bs=4k --iodepth=1 --size=64k \
	> /dev/null 2>&1
ERR_CODE=$?

if [ "$ERR_CODE" -ne 0 ]; then
	kill -9 "$(_get_ublk_daemon_pid "$dev_id" 2>/dev/null)" 2>/dev/null || true
	sleep 0.5
	pkill -9 fio 2>/dev/null || true
	ERR_CODE=1
fi

_cleanup_test
_show_result $TID $ERR_CODE
