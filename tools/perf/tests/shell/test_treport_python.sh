#!/bin/bash
# perf script treport test
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
perfdata=
output=
badfile=

cleanup() {
	[ -n "${perfdata}" ] && rm -f "${perfdata}"*
	rm -f "${output}" "${badfile}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

perfdata=$(mktemp /tmp/__perf_treport_test.perf.data.XXXXX)
output=$(mktemp /tmp/__perf_treport_test.output.XXXXX)
badfile=$(mktemp /tmp/__perf_treport_test.bad.XXXXX)

test_plumbing() {
	echo "treport plumbing test"
	# A missing input file should be reported by the script launched
	# through perf script, this doesn't need a terminal.
	if perf script treport -i "${perfdata}.missing" > "${output}" 2>&1; then
		echo "treport plumbing test [Failed: missing file not an error]"
		err=1
		return
	fi
	if ! grep -q "not found" "${output}"; then
		echo "treport plumbing test [Failed: script not launched]"
		cat "${output}"
		err=1
		return
	fi
	echo "treport plumbing test [Success]"
}

test_bad_file() {
	echo "treport bad file test"
	echo "not a perf.data file" > "${badfile}"
	# The app starts before the file is read, so the error is reported by
	# the app exiting with a failure.
	if ! "$PYTHON" - "${badfile}" > "${output}" 2>&1 <<'EOF'
import asyncio
import sys
import treport

async def run() -> None:
    app = treport.ReportApp(sys.argv[1])
    async with app.run_test(size=(120, 40)) as pilot:
        for _ in range(100):
            if app.return_code is not None:
                break
            await pilot.pause(0.1)
    if app.return_code != 1:
        raise RuntimeError(f"Unexpected return code {app.return_code}")
    print("bad file ok")

asyncio.run(run())
EOF
	then
		echo "treport bad file test [Failed: exception]"
		cat "${output}"
		err=1
		return
	fi
	if ! grep -q "bad file ok" "${output}"; then
		echo "treport bad file test [Failed: unexpected output]"
		cat "${output}"
		err=1
		return
	fi
	echo "treport bad file test [Success]"
}

record() {
	# A software event with callchains that doesn't need privileges.
	perf record -e task-clock -g -o "${perfdata}" -- perf test -w noploop \
		> /dev/null 2>&1
}

test_headless_ui() {
	echo "treport headless UI test"
	# Drive the textual app without a terminal, check the profile shown
	# matches one built without the UI and exercise the key bindings.
	if ! "$PYTHON" - "${perfdata}" > "${output}" 2>&1 <<'EOF'
import asyncio
import sys
import treport

def labels(tree, tnode, out):
    for child in tnode.children:
        out.append(str(child.label))
        if child.is_expanded:
            labels(tree, child, out)
    return out

async def run() -> None:
    expected = treport.ProfileBuilder()
    expected.read(sys.argv[1])
    if not expected.root.children:
        raise RuntimeError("No samples in the profile")

    # The app starts before the profile is built in a background thread.
    app = treport.ReportApp(sys.argv[1])
    async with app.run_test(size=(120, 40)) as pilot:
        for _ in range(600):
            if app.loaded:
                break
            await pilot.pause(0.1)
        if not app.loaded:
            raise RuntimeError("Timed out loading data")
        await pilot.pause()
        if app.sub_title.startswith("Loading") or "samples" not in app.sub_title:
            raise RuntimeError(f"Unexpected sub-title: {app.sub_title}")
        totals = {name: node.value for name, node in app.root.children.items()}
        want = {name: node.value for name, node in expected.root.children.items()}
        if totals != want:
            raise RuntimeError(f"Profile {totals} differs from {want}")
        tree = app.query_one(treport.ProfileTree)
        shown = labels(tree, tree.root, [])
        if not any("noploop" in label for label in shown):
            raise RuntimeError(f"noploop not shown in: {shown}")
        await pilot.press("down", "down", "enter", "up", "enter")
        tabs = app.query_one(treport.TabbedContent)
        tabs.active = "flame"
        await pilot.pause()
        await pilot.press("down", "down", "right", "enter", "escape", "up", "left")
        await pilot.pause()
        print("headless UI ok")

asyncio.run(run())
EOF
	then
		echo "treport headless UI test [Failed: exception]"
		cat "${output}"
		err=1
		return
	fi
	if ! grep -q "headless UI ok" "${output}"; then
		echo "treport headless UI test [Failed: unexpected output]"
		cat "${output}"
		err=1
		return
	fi
	echo "treport headless UI test [Success]"
}

test_cancel() {
	echo "treport cancel test"
	# Quitting while loading should stop the background thread without a
	# "processing failed" error from the session.
	if ! "$PYTHON" - "${perfdata}" > "${output}" 2>&1 <<'EOF'
import asyncio
import sys
import time
import treport

# Check for cancellation often and slow processing each sample so that the
# load can't finish before it is cancelled.
treport.ProfileBuilder.PROGRESS_INTERVAL = 1
process_event = treport.ProfileBuilder.process_event

def slow_process_event(self, sample) -> None:
    time.sleep(0.001)
    process_event(self, sample)

treport.ProfileBuilder.process_event = slow_process_event

async def run() -> None:
    app = treport.ReportApp(sys.argv[1])
    async with app.run_test(size=(120, 40)) as pilot:
        for _ in range(100):
            loading = app.loading
            if loading is not None and loading.nr_samples > 0:
                break
            await pilot.pause(0.1)
        if loading is None or app.loaded:
            raise RuntimeError("Background load not in progress")
        await pilot.press("ctrl+q")
    for _ in range(100):
        if app.loading is None:
            break
        time.sleep(0.1)
    if app.loading is not None:
        raise RuntimeError("Background load didn't stop")
    if not loading.cancelled or app.loaded:
        raise RuntimeError("Background load wasn't cancelled")
    print("cancel ok")

asyncio.run(run())
EOF
	then
		echo "treport cancel test [Failed: exception]"
		cat "${output}"
		err=1
		return
	fi
	if ! grep -q "cancel ok" "${output}" || grep -q "processing failed" "${output}"; then
		echo "treport cancel test [Failed: unexpected output]"
		cat "${output}"
		err=1
		return
	fi
	echo "treport cancel test [Success]"
}

test_plumbing
test_bad_file

if ! record; then
	echo "Skipping remaining tests, failed to record samples"
	if [ $err -eq 0 ]; then
		err=2
	fi
	cleanup
	exit $err
fi

test_headless_ui
test_cancel

cleanup
exit $err
