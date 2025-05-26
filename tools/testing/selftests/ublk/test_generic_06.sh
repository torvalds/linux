#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_06"
ERR_CODE=0

_prep_test "fault_inject" "fast cleanup when all I/Os of one hctx are in server"

# configure ublk server to sleep 2s before completing each I/O
dev_id=$(_add_ublk_dev -t fault_inject -q 2 -d 1 --delay_us 2000000)
_check_add_dev $TID $?

STARTTIME=${SECONDS}

dd if=/dev/urandom of=/dev/ublkb${dev_id} oflag=direct bs=4k count=1 status=none > /dev/null 2>&1 &
dd_pid=$!

__ublk_kill_daemon ${dev_id} "DEAD" >/dev/null

wait $dd_pid
dd_exitcode=$?

ENDTIME=${SECONDS}
ELAPSED=$(($ENDTIME - $STARTTIME))

# assert that dd sees an error and exits quickly after ublk server is
# killed. previously this relied on seeing an I/O timeout and so would
# take ~30s
if [ $dd_exitcode -eq 0 ]; then
        echo "dd unexpectedly exited successfully!"
        ERR_CODE=255
fi
if [ $ELAPSED -ge 5 ]; then
        echo "dd took $ELAPSED seconds to exit (>= 5s tolerance)!"
        ERR_CODE=255
fi

_cleanup_test "fault_inject"
_show_result $TID $ERR_CODE
