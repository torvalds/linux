#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# pstore_tests - Check pstore's behavior before crash/reboot
#
# Copyright (C) Hitachi Ltd., 2015
#  Written by Hiraku Toyooka <hiraku.toyooka.gu@hitachi.com>
#

. ./common_tests

prlog -n "Checking pstore console is registered ... "
dmesg | grep -Eq "console \[(pstore|${backend})"
show_result $?

prlog -n "Checking /dev/pmsg0 exists ... "
test -e /dev/pmsg0
show_result $?

prlog -n "Writing unique string to /dev/pmsg0 ... "
if [ -e "/dev/pmsg0" ]; then
    echo "${TEST_STRING_PATTERN}""$UUID" > /dev/pmsg0
    show_result $?
    echo "$UUID" > $TOP_DIR/uuid
else
    prlog "FAIL"
    rc=1
fi

exit $rc
