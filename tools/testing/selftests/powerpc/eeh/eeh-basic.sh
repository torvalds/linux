#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

. ./eeh-functions.sh

eeh_test_prep # NB: may exit

pre_lspci=`mktemp`
lspci > $pre_lspci

# record the devices that we break in here. Assuming everything
# goes to plan we should get them back once the recover process
# is finished.
devices=""

# Build up a list of candidate devices.
for dev in `ls -1 /sys/bus/pci/devices/ | grep '\.0$'` ; do
	if ! eeh_can_break $dev ; then
		continue;
	fi

	# Skip VFs for now since we don't have a reliable way to break them.
	if [ -e "/sys/bus/pci/devices/$dev/physfn" ] ; then
		echo "$dev, Skipped: virtfn"
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
