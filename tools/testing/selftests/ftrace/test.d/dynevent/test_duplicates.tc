#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Generic dynamic event - check if duplicate events are caught
# requires: dynamic_events "e[:[<group>/][<event>]] <attached-group>.<attached-event> [<args>]":README events/syscalls/sys_enter_openat

echo 0 > events/enable

HAVE_KPROBES=0

if [ -f kprobe_events ]; then
	HAVE_KPROBES=1
fi

clear_dynamic_events

# first create dynamic events for eprobes and kprobes.

echo 'e:egroup/eevent syscalls/sys_enter_openat file=+0($filename):ustring' >> dynamic_events

# Test eprobe for same eprobe, existing kprobe and existing event
! echo 'e:egroup/eevent syscalls/sys_enter_openat file=+0($filename):ustring' >> dynamic_events
! echo 'e:syscalls/sys_enter_open syscalls/sys_enter_openat file=+0($filename):ustring' >> dynamic_events

if [ $HAVE_KPROBES -eq 1 ]; then
    echo 'p:kgroup/kevent vfs_open file=+0($arg2)' >> dynamic_events
    ! echo 'e:kgroup/kevent syscalls/sys_enter_openat file=+0($filename):ustring' >> dynamic_events

# Test kprobe for same kprobe, existing eprobe and existing event
    ! echo 'p:kgroup/kevent vfs_open file=+0($arg2)' >> dynamic_events
    ! echo 'p:egroup/eevent vfs_open file=+0($arg2)' >> dynamic_events
    ! echo 'p:syscalls/sys_enter_open vfs_open file=+0($arg2)' >> dynamic_events

    echo '-:kgroup/kevent' >> dynamic_events
fi

echo '-:egroup/eevent' >> dynamic_events

clear_trace
