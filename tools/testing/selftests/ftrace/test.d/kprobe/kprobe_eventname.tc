#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kprobe event auto/manual naming
# requires: kprobe_events

:;: "Add an event on function without name" ;:

FUNC=`grep " [tT] .*vfs_read$" /proc/kallsyms | tail -n 1 | cut -f 3 -d " "`
[ "x" != "x$FUNC" ] || exit_unresolved
echo "p $FUNC" > kprobe_events
PROBE_NAME=`echo $FUNC | tr ".:" "_"`
test -d events/kprobes/p_${PROBE_NAME}_0 || exit_failure

:;: "Add an event on function with new name" ;:

echo "p:event1 $FUNC" > kprobe_events
test -d events/kprobes/event1 || exit_failure

:;: "Add an event on function with new name and group" ;:

echo "p:kprobes2/event2 $FUNC" > kprobe_events
test -d events/kprobes2/event2 || exit_failure

:;: "Add an event on dot function without name" ;:

find_dot_func() {
	if [ ! -f available_filter_functions ]; then
		grep -m 10 " [tT] .*\.isra\..*$" /proc/kallsyms | tail -n 1 | cut -f 3 -d " "
		return;
	fi

	grep " [tT] .*\.isra\..*" /proc/kallsyms | cut -f 3 -d " " | while read f; do
		if grep -s $f available_filter_functions; then
			echo $f
			break
		fi
	done
}

FUNC=`find_dot_func | tail -n 1`
[ "x" != "x$FUNC" ] || exit_unresolved
echo "p $FUNC" > kprobe_events
EVENT=`grep $FUNC kprobe_events | cut -f 1 -d " " | cut -f 2 -d:`
[ "x" != "x$EVENT" ] || exit_failure
test -d events/$EVENT || exit_failure
