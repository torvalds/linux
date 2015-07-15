#!/bin/sh

###############################################################################
#
#   Copyright © International Business Machines  Corp., 2009
#
#   This program is free software;  you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
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
    tput setf 7
    if [ $? -eq 0 ]; then
        USE_COLOR=1
        tput sgr0
    fi
fi
if [ "$USE_COLOR" -eq 1 ]; then
    COLOR="-c"
fi


echo
# requeue pi testing
# without timeouts
./futex_requeue_pi $COLOR
./futex_requeue_pi $COLOR -b
./futex_requeue_pi $COLOR -b -l
./futex_requeue_pi $COLOR -b -o
./futex_requeue_pi $COLOR -l
./futex_requeue_pi $COLOR -o
# with timeouts
./futex_requeue_pi $COLOR -b -l -t 5000
./futex_requeue_pi $COLOR -l -t 5000
./futex_requeue_pi $COLOR -b -l -t 500000
./futex_requeue_pi $COLOR -l -t 500000
./futex_requeue_pi $COLOR -b -t 5000
./futex_requeue_pi $COLOR -t 5000
./futex_requeue_pi $COLOR -b -t 500000
./futex_requeue_pi $COLOR -t 500000
./futex_requeue_pi $COLOR -b -o -t 5000
./futex_requeue_pi $COLOR -l -t 5000
./futex_requeue_pi $COLOR -b -o -t 500000
./futex_requeue_pi $COLOR -l -t 500000
# with long timeout
./futex_requeue_pi $COLOR -b -l -t 2000000000
./futex_requeue_pi $COLOR -l -t 2000000000


echo
./futex_requeue_pi_mismatched_ops $COLOR

echo
./futex_requeue_pi_signal_restart $COLOR

echo
./futex_wait_timeout $COLOR

echo
./futex_wait_wouldblock $COLOR

echo
./futex_wait_uninitialized_heap $COLOR
./futex_wait_private_mapped_file $COLOR
