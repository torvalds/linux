#!/bin/bash
# perf script ilist and perf list --tui test (no_mlock)
# SPDX-License-Identifier: GPL-2.0

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

if ! "$PYTHON" -c 'import textual' > /dev/null 2>&1; then
	echo "Skipping test, python textual library not found"
	exit 2
fi

err=0
output=$(mktemp /tmp/__perf_ilist_test.output.XXXXX)

cleanup() {
	rm -f "${output}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

test_plumbing() {
	echo "ilist plumbing test"
	# The script should be found and run by perf script, --help doesn't
	# need a terminal.
	if ! perf script ilist --help > "${output}" 2>&1; then
		echo "ilist plumbing test [Failed: script failed]"
		cat "${output}"
		err=1
		return
	fi
	if ! grep -q -- "--interval" "${output}"; then
		echo "ilist plumbing test [Failed: script not launched]"
		cat "${output}"
		err=1
		return
	fi
	echo "ilist plumbing test [Success]"
}

test_list_tui() {
	echo "perf list --tui plumbing test"
	# Arguments after -- are passed to the script launched by perf list.
	if ! perf list --tui -- --help > "${output}" 2>&1; then
		echo "perf list --tui plumbing test [Failed: script failed]"
		cat "${output}"
		err=1
		return
	fi
	if ! grep -q -- "--interval" "${output}"; then
		echo "perf list --tui plumbing test [Failed: script not launched]"
		cat "${output}"
		err=1
		return
	fi
	echo "perf list --tui plumbing test [Success]"
}

test_headless_ui() {
	echo "ilist headless UI test"
	# Drive the textual app without a terminal. Opening counters may fail
	# for want of permissions, in which case an error dialog is shown.
	if ! "$PYTHON" > "${output}" 2>&1 <<'EOF'
import asyncio
import ilist
from textual.widgets import Label, Tree

async def dismiss_error(app, pilot) -> bool:
    """Close the error dialog if shown, returning whether it was."""
    if not isinstance(app.screen, ilist.ErrorScreen):
        return False
    await pilot.click("#error")
    await pilot.pause()
    if isinstance(app.screen, ilist.ErrorScreen):
        raise RuntimeError("Error dialog not dismissed")
    return True

async def search(pilot, text: str) -> None:
    await pilot.press("s")
    await pilot.pause()
    await pilot.press(*text, "enter")
    await pilot.pause()

def find_leaf(node, cls):
    """Find the first node whose data is of type cls."""
    if isinstance(node.data, cls):
        return node
    for child in node.children:
        found = find_leaf(child, cls)
        if found:
            return found
    return None

async def check_counting(app, pilot, name: str) -> None:
    """Check the selected event is shown and counting, or that it failed to open."""
    await pilot.pause(0.5)
    if await dismiss_error(app, pilot):
        print(f"{name}: failed to open")
        return
    shown = str(app.query_one("#event_name", Label).render())
    if shown != name:
        raise RuntimeError(f"Selected {shown} rather than {name}")
    if not app.query(ilist.CounterSparkline) or not app.query("#counter_total"):
        raise RuntimeError(f"No counters shown for {name}")
    print(f"{name}: total {app.query_one('#counter_total', Label).render()}")

async def run() -> None:
    app = ilist.IListApp(0.1)
    async with app.run_test(size=(120, 40)) as pilot:
        await pilot.pause()
        tree = app.query_one("#root", Tree)
        top = [str(node.label) for node in tree.root.children]
        if top != ["PMUs", "Metrics"]:
            raise RuntimeError(f"Unexpected tree: {top}")
        pmus = [str(node.label) for node in tree.root.children[0].children]
        if "software" not in pmus:
            raise RuntimeError(f"No software PMU in: {pmus}")

        # Search for a software event, which exists everywhere. The search
        # result is selected which opens the event.
        await search(pilot, "task-clock")
        found = app.cur_search_result
        if not found or not isinstance(found.data, ilist.PmuEvent) or \
           "task-clock" not in str(found.label):
            raise RuntimeError(f"Search didn't find task-clock: {found}")
        await check_counting(app, pilot, found.data.name())
        if not app.query_one("#active_search", Label).display:
            raise RuntimeError("Active search not shown")
        await pilot.press("n", "p", "down", "up", "c")
        await pilot.pause(0.2)
        await dismiss_error(app, pilot)

        # Searching for something that doesn't exist shows an error.
        await search(pilot, "no-such-event-xyzzy")
        if not await dismiss_error(app, pilot):
            raise RuntimeError("No error for a failed search")

        # Select a metric, if there are any.
        metric = find_leaf(tree.root, ilist.Metric)
        if metric:
            app.expand_and_select(metric)
            await check_counting(app, pilot, metric.data.name())
        print("headless UI ok")

asyncio.run(run())
EOF
	then
		echo "ilist headless UI test [Failed: exception]"
		cat "${output}"
		err=1
		return
	fi
	if ! grep -q "headless UI ok" "${output}"; then
		echo "ilist headless UI test [Failed: unexpected output]"
		cat "${output}"
		err=1
		return
	fi
	echo "ilist headless UI test [Success]"
}

test_plumbing
test_list_tui
test_headless_ui

cleanup
exit $err
