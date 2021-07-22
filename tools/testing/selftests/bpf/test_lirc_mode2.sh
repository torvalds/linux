#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

msg="skip all tests:"
if [ $UID != 0 ]; then
	echo $msg please run this as root >&2
	exit $ksft_skip
fi

GREEN='\033[0;92m'
RED='\033[0;31m'
NC='\033[0m' # No Color

modprobe rc-loopback

for i in /sys/class/rc/rc*
do
	if grep -q DRV_NAME=rc-loopback $i/uevent
	then
		LIRCDEV=$(grep DEVNAME= $i/lirc*/uevent | sed sQDEVNAME=Q/dev/Q)
		INPUTDEV=$(grep DEVNAME= $i/input*/event*/uevent | sed sQDEVNAME=Q/dev/Q)
	fi
done

if [ -n $LIRCDEV ];
then
	TYPE=lirc_mode2
	./test_lirc_mode2_user $LIRCDEV $INPUTDEV
	ret=$?
	if [ $ret -ne 0 ]; then
		echo -e ${RED}"FAIL: $TYPE"${NC}
	else
		echo -e ${GREEN}"PASS: $TYPE"${NC}
	fi
fi
