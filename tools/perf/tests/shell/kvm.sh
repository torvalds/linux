#!/bin/bash
# perf kvm tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_kvm_test.perf.data.XXXXX)
qemu_pid_file=$(mktemp /tmp/__perf_kvm_test.qemu.pid.XXXXX)

cleanup() {
	rm -f "${perfdata}"
	if [ -f "${qemu_pid_file}" ]; then
		if [ -s "${qemu_pid_file}" ]; then
			qemu_pid=$(cat "${qemu_pid_file}")
			if [ -n "${qemu_pid}" ]; then
				kill "${qemu_pid}" 2>/dev/null || true
			fi
		fi
		rm -f "${qemu_pid_file}"
	fi
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

skip() {
	echo "Skip: $1"
	cleanup
	exit 2
}

test_kvm_stat() {
	echo "Testing perf kvm stat"

	echo "Recording kvm events for pid ${qemu_pid}..."
	if ! perf kvm stat record -p "${qemu_pid}" -o "${perfdata}" sleep 1; then
		echo "Failed to record kvm events"
		err=1
		return
	fi

	echo "Reporting kvm events..."
	if ! perf kvm -i "${perfdata}" stat report 2>&1 | grep -q "VM-EXIT"; then
		echo "Failed to find VM-EXIT in report"
		perf kvm -i "${perfdata}" stat report 2>&1
		err=1
		return
	fi

	echo "perf kvm stat test [Success]"
}

test_kvm_record_report() {
	echo "Testing perf kvm record/report"

	echo "Recording kvm profile for pid ${qemu_pid}..."
	# Use --host to avoid needing guest symbols/mounts for this simple test
	# We just want to verify the command runs and produces data
	# We run in background and kill it because 'perf kvm record' appends options
	# after the command, which breaks 'sleep' (e.g. it gets '-e cycles').
	perf kvm --host record -p "${qemu_pid}" -o "${perfdata}" &
	rec_pid=$!
	sleep 1
	kill -INT "${rec_pid}"
	wait "${rec_pid}" || true

	echo "Reporting kvm profile..."
	# Check for some standard output from report
	if ! perf kvm -i "${perfdata}" report --stdio 2>&1 | grep -q "Event count"; then
		echo "Failed to report kvm profile"
		perf kvm -i "${perfdata}" report --stdio 2>&1
		err=1
		return
	fi

	echo "perf kvm record/report test [Success]"
}

test_kvm_buildid_list() {
	echo "Testing perf kvm buildid-list"

	# We reuse the perf.data from the previous record test
	if ! perf kvm --host -i "${perfdata}" buildid-list 2>&1 | grep -q "."; then
		echo "Failed to list buildids"
		perf kvm --host -i "${perfdata}" buildid-list 2>&1
		err=1
		return
	fi

	echo "perf kvm buildid-list test [Success]"
}

setup_qemu() {
	# Find qemu
	if [ "$(uname -m)" = "x86_64" ]; then
		qemu="qemu-system-x86_64"
	elif [ "$(uname -m)" = "aarch64" ]; then
		qemu="qemu-system-aarch64"
	elif [ "$(uname -m)" = "s390x" ]; then
		qemu="qemu-system-s390x"
	elif [ "$(uname -m)" = "ppc64le" ]; then
		qemu="qemu-system-ppc64"
	else
		qemu="qemu-system-$(uname -m)"
	fi

	if ! which -s "$qemu"; then
		skip "$qemu not found"
	fi

	if [ ! -r /dev/kvm ] || [ ! -w /dev/kvm ]; then
		skip "/dev/kvm not accessible"
	fi

	if ! perf kvm stat record -o /dev/null -a sleep 0.01 >/dev/null 2>&1; then
		skip "No permission to record kvm events"
	fi

	echo "Starting $qemu..."
	# Start qemu in background, detached, with pidfile
	# We use -display none -daemonize and a monitor to keep it alive/controllable if needed
	# We don't need a real kernel, just KVM active.
	if ! $qemu -enable-kvm -display none -daemonize -pidfile "${qemu_pid_file}" -monitor none; then
		echo "Failed to start qemu"
		err=1
		return
	fi

	# Wait a bit for qemu to start
	sleep 1
	qemu_pid=$(cat "${qemu_pid_file}")

	if ! kill -0 "${qemu_pid}" 2>/dev/null; then
		echo "Qemu process failed to stay alive"
		err=1
		return
	fi
}

setup_qemu
if [ $err -eq 0 ]; then
	test_kvm_stat
	test_kvm_record_report
	test_kvm_buildid_list
fi

cleanup
exit $err
