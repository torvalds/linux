#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

_prep_test "null" "stop --safe command"

# Check if SAFE_STOP_DEV feature is supported
if ! _have_feature "SAFE_STOP_DEV"; then
	_cleanup_test "null"
	exit "$UBLK_SKIP_CODE"
fi

# Test 1: stop --safe on idle device should succeed
dev_id=$(_add_ublk_dev -t null -q 2 -d 32)
_check_add_dev $TID $?

# Device is idle (no openers), stop --safe should succeed
if ! ${UBLK_PROG} stop -n "${dev_id}" --safe; then
	echo "stop --safe on idle device failed unexpectedly!"
	ERR_CODE=255
fi

# Clean up device
_ublk_del_dev "${dev_id}" > /dev/null 2>&1
udevadm settle

# Test 2: stop --safe on device with active opener should fail
dev_id=$(_add_ublk_dev -t null -q 2 -d 32)
_check_add_dev $TID $?

# Open device in background (dd reads indefinitely)
dd if=/dev/ublkb${dev_id} of=/dev/null bs=4k iflag=direct > /dev/null 2>&1 &
dd_pid=$!

# Give dd time to start
sleep 0.2

# Device has active opener, stop --safe should fail with -EBUSY
if ${UBLK_PROG} stop -n "${dev_id}" --safe 2>/dev/null; then
	echo "stop --safe on busy device succeeded unexpectedly!"
	ERR_CODE=255
fi

# Kill dd and clean up
kill $dd_pid 2>/dev/null
wait $dd_pid 2>/dev/null

# Now device should be idle, regular delete should work
_ublk_del_dev "${dev_id}"
udevadm settle

_cleanup_test "null"
_show_result $TID $ERR_CODE
