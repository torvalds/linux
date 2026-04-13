#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

_prep_test "fault_inject" "teardown after incomplete recovery"

# First start and stop a ublk server with device configured for recovery
dev_id=$(_add_ublk_dev -t fault_inject -r 1)
_check_add_dev $TID $?
state=$(__ublk_kill_daemon "${dev_id}" "QUIESCED")
if [ "$state" != "QUIESCED" ]; then
        echo "device isn't quiesced($state) after $action"
        ERR_CODE=255
fi

# Then recover the device, but use --die_during_fetch to have the ublk
# server die while a queue has some (but not all) I/Os fetched
${UBLK_PROG} recover -n "${dev_id}" --foreground -t fault_inject --die_during_fetch 1
RECOVER_RES=$?
# 137 is the result when dying of SIGKILL
if (( RECOVER_RES != 137 )); then
        echo "recover command exited with unexpected code ${RECOVER_RES}!"
        ERR_CODE=255
fi

# Clean up the device. This can only succeed once teardown of the above
# exited ublk server completes. So if teardown never completes, we will
# time out here
_ublk_del_dev "${dev_id}"

_cleanup_test "fault_inject"
_show_result $TID $ERR_CODE
