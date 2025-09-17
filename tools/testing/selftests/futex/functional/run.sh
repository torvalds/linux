#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

###############################################################################
#
#   Copyright © International Business Machines  Corp., 2009
#
# DESCRIPTION
#      Run tests in the current directory.
#
# AUTHOR
#      Darren Hart <dvhart@linux.intel.com>
#
# HISTORY
#      2009-Nov-9: Initial version by Darren Hart <dvhart@linux.intel.com>
#      2010-Jan-6: Add futex_wait_uninitialized_heap and futex_wait_private_mapped_file
#                  by KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
#
###############################################################################

# Test for a color capable console
if [ -z "$USE_COLOR" ]; then
    tput setf 7 || tput setaf 7
    if [ $? -eq 0 ]; then
        USE_COLOR=1
        tput sgr0
    fi
fi
if [ "$USE_COLOR" -eq 1 ]; then
    COLOR="-c"
fi


echo
./futex_requeue_pi

echo
./futex_requeue_pi_mismatched_ops

echo
./futex_requeue_pi_signal_restart

echo
./futex_wait_timeout

echo
./futex_wait_wouldblock

echo
./futex_wait_uninitialized_heap
./futex_wait_private_mapped_file

echo
./futex_wait

echo
./futex_requeue

echo
./futex_waitv

echo
./futex_priv_hash

echo
./futex_numa_mpol $COLOR
