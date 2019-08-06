#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

if ! /sbin/modprobe -q -n rc-loopback; then
        echo "ir_loopback: module rc-loopback is not found [SKIP]"
        exit $ksft_skip
fi

/sbin/modprobe rc-loopback
if [ $? -ne 0 ]; then
	exit
fi

RCDEV=$(grep -l DRV_NAME=rc-loopback /sys/class/rc/rc*/uevent | grep -o 'rc[0-9]\+')

./ir_loopback $RCDEV $RCDEV
exit
