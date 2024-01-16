#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# pstore_post_reboot_tests - Check pstore's behavior after crash/reboot
#
# Copyright (C) Hitachi Ltd., 2015
#  Written by Hiraku Toyooka <hiraku.toyooka.gu@hitachi.com>
#

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

. ./common_tests

if [ -e $REBOOT_FLAG  ]; then
    rm $REBOOT_FLAG
else
    prlog "pstore_crash_test has not been executed yet. we skip further tests."
    exit $ksft_skip
fi

prlog -n "Mounting pstore filesystem ... "
mount_info=`grep pstore /proc/mounts`
if [ $? -eq 0 ]; then
    mount_point=`echo ${mount_info} | cut -d' ' -f2 | head -n1`
    prlog "ok"
else
    mount none /sys/fs/pstore -t pstore
    if [ $? -eq 0 ]; then
	mount_point=`grep pstore /proc/mounts | cut -d' ' -f2 | head -n1`
	prlog "ok"
    else
	prlog "FAIL"
	exit 1
    fi
fi

cd ${mount_point}

prlog -n "Checking dmesg files exist in pstore filesystem ... "
check_files_exist dmesg

prlog -n "Checking console files exist in pstore filesystem ... "
check_files_exist console

prlog -n "Checking pmsg files exist in pstore filesystem ... "
check_files_exist pmsg

prlog -n "Checking dmesg files contain oops end marker"
grep_end_trace() {
    grep -q "\---\[ end trace" $1
}
files=`ls dmesg-${backend}-*`
operate_files $? "$files" grep_end_trace

prlog -n "Checking console file contains oops end marker ... "
grep -q "\---\[ end trace" console-${backend}-0
show_result $?

prlog -n "Checking pmsg file properly keeps the content written before crash ... "
prev_uuid=`cat $TOP_DIR/prev_uuid`
if [ $? -eq 0 ]; then
    nr_matched=`grep -c "$TEST_STRING_PATTERN" pmsg-${backend}-0`
    if [ $nr_matched -eq 1 ]; then
	grep -q "$TEST_STRING_PATTERN"$prev_uuid pmsg-${backend}-0
	show_result $?
    else
	prlog "FAIL"
	rc=1
    fi
else
    prlog "FAIL"
    rc=1
fi

prlog -n "Removing all files in pstore filesystem "
files=`ls *-${backend}-*`
operate_files $? "$files" rm

exit $rc
