#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Function return probe entry argument access
# requires: dynamic_events 'f[:[<group>/][<event>]] <func-name>':README 'kernel return probes support:':README

echo 'f:tests/myevent1 vfs_open arg=$arg1' >> dynamic_events
echo 'f:tests/myevent2 vfs_open%return arg=$arg1' >> dynamic_events

echo 1 > events/tests/enable

echo > trace
cat trace > /dev/null

streq() {
	test $1 = $2
}

streq `grep -A 1 -m 1 myevent1 trace | sed -r 's/^.*(arg=.*)/\1/' `
