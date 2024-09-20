#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kretprobe entry argument access
# requires: kprobe_events 'kernel return probes support:':README

echo 'p:myevent1 vfs_open arg=$arg1' >> kprobe_events
echo 'r:myevent2 vfs_open arg=$arg1' >> kprobe_events

echo 1 > events/kprobes/enable

echo > trace
cat trace > /dev/null

streq() {
	test $1 = $2
}

streq `grep -A 1 -m 1 myevent1 trace | sed -r 's/^.*(arg=.*)/\1/' `
