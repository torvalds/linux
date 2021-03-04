#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

KSELFTESTS_SKIP=4

. ./eeh-functions.sh

if ! eeh_supported ; then
	echo "EEH not supported on this system, skipping"
	exit $KSELFTESTS_SKIP;
fi

if [ ! -e "/sys/kernel/debug/powerpc/eeh_dev_check" ] && \
   [ ! -e "/sys/kernel/debug/powerpc/eeh_dev_break" ] ; then
	echo "debugfs EEH testing files are missing. Is debugfs mounted?"
	exit $KSELFTESTS_SKIP;
fi

pre_lspci=`mktemp`
lspci > $pre_lspci

# Bump the max freeze count to something absurd so we don't
# trip over it while breaking things.
echo 5000 > /sys/kernel/debug/powerpc/eeh_max_freezes

# record the devices that we break in here. Assuming everything
# goes to plan we should get them back once the recover process
# is finished.
devices=""

# Build up a list of candidate devices.
for dev in `ls -1 /sys/bus/pci/devices/ | grep '\.0$'` ; do
	# skip bridges since we can't recover them (yet...)
	if [ -e "/sys/bus/pci/devices/$dev/pci_bus" ] ; then
		echo "$dev, Skipped: bridge"
		continue;
	fi

	# Skip VFs for now since we don't have a reliable way
	# to break them.
	if [ -e "/sys/bus/pci/devices/$dev/physfn" ] ; then
		echo "$dev, Skipped: virtfn"
		continue;
	fi

	if [ "ahci" = "$(basename $(realpath /sys/bus/pci/devices/$dev/driver))" ] ; then
		echo "$dev, Skipped: ahci doesn't support recovery"
		continue
	fi

	# Don't inject errosr into an already-frozen PE. This happens with
	# PEs that contain multiple PCI devices (e.g. multi-function cards)
	# and injecting new errors during the recovery process will probably
	# result in the recovery failing and the device being marked as
	# failed.
	if ! pe_ok $dev ; then
		echo "$dev, Skipped: Bad initial PE state"
		continue;
	fi

	echo "$dev, Added"

	# Add to this list of device to check
	devices="$devices $dev"
done

dev_count="$(echo $devices | wc -w)"
echo "Found ${dev_count} breakable devices..."

failed=0
for dev in $devices ; do
	echo "Breaking $dev..."

	if ! pe_ok $dev ; then
		echo "Skipping $dev, Initial PE state is not ok"
		failed="$((failed + 1))"
		continue;
	fi

	if ! eeh_one_dev $dev ; then
		failed="$((failed + 1))"
	fi
done

echo "$failed devices failed to recover ($dev_count tested)"
lspci | diff -u $pre_lspci -
rm -f $pre_lspci

test "$failed" -eq 0
exit $?
