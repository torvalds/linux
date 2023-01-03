#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test inter-event histogram trigger eprobe on synthetic event
# requires: dynamic_events synthetic_events events/syscalls/sys_enter_openat/hist "e[:[<group>/][<event>]] <attached-group>.<attached-event> [<args>]":README

echo 0 > events/enable

clear_dynamic_events

SYSTEM="syscalls"
START="sys_enter_openat"
END="sys_exit_openat"
FIELD="filename"
SYNTH="synth_open"
EPROBE="eprobe_open"

echo "$SYNTH unsigned long filename; long ret;" > synthetic_events
echo "hist:keys=common_pid:__arg__1=$FIELD" > events/$SYSTEM/$START/trigger
echo "hist:keys=common_pid:filename=\$__arg__1,ret=ret:onmatch($SYSTEM.$START).trace($SYNTH,\$filename,\$ret)" > events/$SYSTEM/$END/trigger

echo "e:$EPROBE synthetic/$SYNTH file=+0(\$filename):ustring ret=\$ret:s64" >> dynamic_events

grep -q "$SYNTH" dynamic_events
grep -q "$EPROBE" dynamic_events
test -d events/synthetic/$SYNTH
test -d events/eprobes/$EPROBE

echo 1 > events/eprobes/$EPROBE/enable
ls
echo 0 > events/eprobes/$EPROBE/enable

content=`grep '^ *ls-' trace | grep 'file='`
nocontent=`grep '^ *ls-' trace | grep 'file=' | grep -v -e '"/' -e '"."'` || true

if [ -z "$content" ]; then
	exit_fail
fi

if [ ! -z "$nocontent" ]; then
	exit_fail
fi

echo "-:$EPROBE" >> dynamic_events
echo '!'"hist:keys=common_pid:filename=\$__arg__1,ret=ret:onmatch($SYSTEM.$START).trace($SYNTH,\$filename,\$ret)" > events/$SYSTEM/$END/trigger
echo '!'"hist:keys=common_pid:__arg__1=$FIELD" > events/$SYSTEM/$START/trigger
echo '!'"$SYNTH u64 filename; s64 ret;" >> synthetic_events

! grep -q "$SYNTH" dynamic_events
! grep -q "$EPROBE" dynamic_events
! test -d events/synthetic/$SYNTH
! test -d events/eprobes/$EPROBE

clear_trace
