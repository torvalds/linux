#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

export KSELFTESTS_SKIP=4

log() {
	echo >/dev/stderr $*
}

pe_ok() {
	local dev="$1"
	local path="/sys/bus/pci/devices/$dev/eeh_pe_state"

	# if a driver doesn't support the error handling callbacks then the
	# device is recovered by removing and re-probing it. This causes the
	# sysfs directory to disappear so read the PE state once and squash
	# any potential error messages
	local eeh_state="$(cat $path 2>/dev/null)"
	if [ -z "$eeh_state" ]; then
		return 1;
	fi

	local fw_state="$(echo $eeh_state | cut -d' ' -f1)"
	local sw_state="$(echo $eeh_state | cut -d' ' -f2)"

	# If EEH_PE_ISOLATED or EEH_PE_RECOVERING are set then the PE is in an
	# error state or being recovered. Either way, not ok.
	if [ "$((sw_state & 0x3))" -ne 0 ] ; then
		return 1
	fi

	# A functioning PE should have the EEH_STATE_MMIO_ACTIVE and
	# EEH_STATE_DMA_ACTIVE flags set. For some goddamn stupid reason
	# the platform backends set these when the PE is in reset. The
	# RECOVERING check above should stop any false positives though.
	if [ "$((fw_state & 0x18))" -ne "$((0x18))" ] ; then
		return 1
	fi

	return 0;
}

eeh_supported() {
	test -e /proc/powerpc/eeh && \
	grep -q 'EEH Subsystem is enabled' /proc/powerpc/eeh
}

eeh_test_prep() {
	if ! eeh_supported ; then
		echo "EEH not supported on this system, skipping"
		exit $KSELFTESTS_SKIP;
	fi

	if [ ! -e "/sys/kernel/debug/powerpc/eeh_dev_check" ] && \
	   [ ! -e "/sys/kernel/debug/powerpc/eeh_dev_break" ] ; then
		log "debugfs EEH testing files are missing. Is debugfs mounted?"
		exit $KSELFTESTS_SKIP;
	fi

	# Bump the max freeze count to something absurd so we don't
	# trip over it while breaking things.
	echo 5000 > /sys/kernel/debug/powerpc/eeh_max_freezes
}

eeh_can_break() {
	# skip bridges since we can't recover them (yet...)
	if [ -e "/sys/bus/pci/devices/$dev/pci_bus" ] ; then
		log "$dev, Skipped: bridge"
		return 1;
	fi

	# The ahci driver doesn't support error recovery. If the ahci device
	# happens to be hosting the root filesystem, and then we go and break
	# it the system will generally go down. We should probably fix that
	# at some point
	if [ "ahci" = "$(basename $(realpath /sys/bus/pci/devices/$dev/driver))" ] ; then
		log "$dev, Skipped: ahci doesn't support recovery"
		return 1;
	fi

	# Don't inject errosr into an already-frozen PE. This happens with
	# PEs that contain multiple PCI devices (e.g. multi-function cards)
	# and injecting new errors during the recovery process will probably
	# result in the recovery failing and the device being marked as
	# failed.
	if ! pe_ok $dev ; then
		log "$dev, Skipped: Bad initial PE state"
		return 1;
	fi

	return 0
}

eeh_one_dev() {
	local dev="$1"

	# Using this function from the command line is sometimes useful for
	# testing so check that the argument is a well-formed sysfs device
	# name.
	if ! test -e /sys/bus/pci/devices/$dev/ ; then
		log "Error: '$dev' must be a sysfs device name (DDDD:BB:DD.F)"
		return 1;
	fi

	# Break it
	echo $dev >/sys/kernel/debug/powerpc/eeh_dev_break

	# Force an EEH device check. If the kernel has already
	# noticed the EEH (due to a driver poll or whatever), this
	# is a no-op.
	echo $dev >/sys/kernel/debug/powerpc/eeh_dev_check

	# Default to a 60s timeout when waiting for a device to recover. This
	# is an arbitrary default which can be overridden by setting the
	# EEH_MAX_WAIT environmental variable when required.

	# The current record holder for longest recovery time is:
	#  "Adaptec Series 8 12G SAS/PCIe 3" at 39 seconds
	max_wait=${EEH_MAX_WAIT:=60}

	for i in `seq 0 ${max_wait}` ; do
		if pe_ok $dev ; then
			break;
		fi
		log "$dev, waited $i/${max_wait}"
		sleep 1
	done

	if ! pe_ok $dev ; then
		log "$dev, Failed to recover!"
		return 1;
	fi

	log "$dev, Recovered after $i seconds"
	return 0;
}

