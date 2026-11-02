#!/bin/bash
# perf timechart --tui (ttimechart.py) test
# SPDX-License-Identifier: GPL-2.0

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! perf check feature -q libtraceevent; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

if ! "$PYTHON" -c 'import textual' > /dev/null 2>&1; then
	echo "Skipping test, python textual library not found"
	exit 2
fi

err=0
perfdata=
output=
clockdata=

cleanup() {
	[ -n "${perfdata}" ] && rm -f "${perfdata}"*
	[ -n "${clockdata}" ] && rm -f "${clockdata}"*
	rm -f "${output}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

perfdata=$(mktemp /tmp/__perf_ttimechart_test.perf.data.XXXXX)
output=$(mktemp /tmp/__perf_ttimechart_test.output.XXXXX)
clockdata=$(mktemp /tmp/__perf_ttimechart_test.clock.data.XXXXX)

workload() {
	for _ in 1 2 3 4 5; do
		sleep 0.01
	done
}

record() {
	# perf timechart record is system wide and so may need privileges,
	# fall back to recording just the workload's scheduler events.
	if perf timechart record -o "${perfdata}" -- \
		bash -c "$(declare -f workload); workload" > /dev/null 2>&1; then
		return 0
	fi
	if perf record -R -c 1 -e sched:sched_switch -e sched:sched_wakeup \
		-o "${perfdata}" -- bash -c "$(declare -f workload); workload" \
		> /dev/null 2>&1; then
		return 0
	fi
	return 1
}

test_plumbing() {
	echo "perf timechart --tui plumbing test"
	# A missing input file should be reported by the script launched
	# through perf script, this doesn't need a terminal.
	if perf timechart --tui -i "${perfdata}.missing" > "${output}" 2>&1; then
		echo "perf timechart --tui plumbing test [Failed: missing file not an error]"
		err=1
		return
	fi
	if ! grep -q "not found" "${output}"; then
		echo "perf timechart --tui plumbing test [Failed: script not launched]"
		cat "${output}"
		err=1
		return
	fi
	echo "perf timechart --tui plumbing test [Success]"
}

test_no_events() {
	echo "ttimechart no events test"
	if ! perf record -e task-clock -o "${clockdata}" -- true > /dev/null 2>&1; then
		echo "ttimechart no events test [Skipped: perf record failed]"
		return
	fi
	if perf script ttimechart -i "${clockdata}" --dump > "${output}" 2>&1; then
		echo "ttimechart no events test [Failed: expected an error]"
		err=1
		return
	fi
	if ! grep -q "no scheduler, power or I/O events" "${output}"; then
		echo "ttimechart no events test [Failed: unexpected error message]"
		cat "${output}"
		err=1
		return
	fi
	# The same should be reported when reading the data from a pipe.
	if perf record -e task-clock -o - -- true 2> /dev/null | \
		perf script ttimechart -i - --dump > "${output}" 2>&1; then
		echo "ttimechart no events test [Failed: expected an error from a pipe]"
		err=1
		return
	fi
	if ! grep -q "no scheduler, power or I/O events" "${output}"; then
		echo "ttimechart no events test [Failed: unexpected error message from a pipe]"
		cat "${output}"
		err=1
		return
	fi
	echo "ttimechart no events test [Success]"
}

test_dump() {
	echo "ttimechart dump test"
	if ! perf script ttimechart -i "${perfdata}" --dump > "${output}" 2>&1; then
		echo "ttimechart dump test [Failed: script failed]"
		cat "${output}"
		err=1
		return
	fi
	if ! grep -q "^Duration: .*sched events: [1-9]" "${output}" ||
	   ! grep -q "^CPU [0-9]*: busy" "${output}" ||
	   ! grep -q "^Task .*Running .*Waiting .*Blocked" "${output}" ||
	   ! grep -q "sleep" "${output}"; then
		echo "ttimechart dump test [Failed: unexpected output]"
		cat "${output}"
		err=1
		return
	fi

	# Tasks only shouldn't show CPUs, power only shouldn't show tasks.
	perf script ttimechart -i "${perfdata}" --dump -T > "${output}" 2>&1 || true
	if grep -q "^CPU [0-9]*: " "${output}" ||
	   ! grep -q "^Task .*Running .*Waiting .*Blocked" "${output}"; then
		echo "ttimechart dump test [Failed: -T output]"
		cat "${output}"
		err=1
		return
	fi
	perf script ttimechart -i "${perfdata}" --dump -P > "${output}" 2>&1 || true
	if grep -q "^Task .*Running .*Waiting .*Blocked" "${output}" ||
	   ! grep -q "^CPU [0-9]*: " "${output}"; then
		echo "ttimechart dump test [Failed: -P output]"
		cat "${output}"
		err=1
		return
	fi

	# Filtering on the workload shouldn't show other tasks.
	perf script ttimechart -i "${perfdata}" --dump -p sleep > "${output}" 2>&1 || true
	if ! grep -q "^sleep " "${output}" || grep -q "^bash " "${output}"; then
		echo "ttimechart dump test [Failed: -p output]"
		cat "${output}"
		err=1
		return
	fi
	echo "ttimechart dump test [Success]"
}

test_headless_ui() {
	echo "ttimechart headless UI test"
	# Drive the textual app without a terminal, exercising the views and
	# key bindings.
	if ! "$PYTHON" - "${perfdata}" > "${output}" 2>&1 <<'EOF'
import asyncio
import sys
import ttimechart as tc

async def run() -> None:
    # The app starts before the data is loaded in a background thread.
    app = tc.TimechartApp(sys.argv[1], False, False, [])
    async with app.run_test(size=(120, 40)) as pilot:
        for _ in range(600):
            if app.loaded:
                break
            await pilot.pause(0.1)
        if not app.loaded:
            raise RuntimeError("Timed out loading data")
        await pilot.pause()
        tabs = app.query_one(tc.TabbedContent)
        for tab in ["cpus", "tasks", "summary"]:
            tabs.active = tab
            await pilot.pause()
            await pilot.press("down", "plus", "plus", "right", "n", "p", "minus",
                              "shift+right", "shift+left", "s", "w", "0")
            await pilot.pause()
        tabs.active = "summary"
        await pilot.pause()
        await pilot.press("enter")
        await pilot.pause()
        details = str(app.query_one("#details", tc.Static).render())
        if "Cursor" not in details:
            raise RuntimeError(f"Unexpected details: {details}")
        print("headless UI ok")

asyncio.run(run())
EOF
	then
		echo "ttimechart headless UI test [Failed: exception]"
		cat "${output}"
		err=1
		return
	fi
	if ! grep -q "headless UI ok" "${output}"; then
		echo "ttimechart headless UI test [Failed: unexpected output]"
		cat "${output}"
		err=1
		return
	fi
	echo "ttimechart headless UI test [Success]"
}

test_plumbing
test_no_events

if ! record; then
	echo "Skipping remaining tests, failed to record scheduler events (permissions?)"
	if [ $err -eq 0 ]; then
		err=2
	fi
	cleanup
	exit $err
fi

test_dump
test_headless_ui

cleanup
exit $err
