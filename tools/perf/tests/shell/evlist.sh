#!/bin/bash
# perf evlist tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)

cleanup() {
	rm -f "${perfdata}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

test_evlist_simple() {
	echo "Simple evlist test"
	if ! perf record -e cycles -o "${perfdata}" true 2> /dev/null
	then
		echo "Simple evlist [Failed record]"
		err=1
		return
	fi
	if ! perf evlist -i "${perfdata}" | grep -q "cycles"
	then
		echo "Simple evlist [Failed to list event]"
		err=1
		return
	fi
	echo "Simple evlist test [Success]"
}

test_evlist_group() {
	echo "Group evlist test"
	if ! perf record -e "{cycles,instructions}" -o "${perfdata}" true 2> /dev/null
	then
		echo "Group evlist [Skipped event group recording failed]"
		return
	fi

	if ! perf evlist -i "${perfdata}" -g | grep -q "{.*cycles.*,.*instructions.*}"
	then
		echo "Group evlist [Failed to list event group]"
		err=1
		return
	fi
	echo "Group evlist test [Success]"
}

test_evlist_verbose() {
	echo "Event configuration evlist test"
	if ! perf record -e cycles -o "${perfdata}" true 2> /dev/null
	then
		echo "Event configuration evlist [Failed record]"
		err=1
		return
	fi

	if ! perf evlist -i "${perfdata}" -v | grep -q "config:"
	then
		echo "Event configuration evlist [Failed to list verbose info]"
		err=1
		return
	fi
	echo "Event configuration evlist test [Success]"
}

test_evlist_simple
test_evlist_group
test_evlist_verbose

cleanup
exit $err
