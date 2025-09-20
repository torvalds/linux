#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -e

TESTNAME=set_pcie_speed

declare -a PCIELINKSPEED=(
	"2.5 GT/s PCIe"
	"5.0 GT/s PCIe"
	"8.0 GT/s PCIe"
	"16.0 GT/s PCIe"
	"32.0 GT/s PCIe"
	"64.0 GT/s PCIe"
)

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
retval=0

coolingdev="$1"
statefile="$coolingdev/cur_state"
maxfile="$coolingdev/max_state"
linkspeedfile="$2"

oldstate=`cat $statefile`
maxstate=`cat $maxfile`

set_state()
{
	local state=$1
	local linkspeed
	local expected_linkspeed

	echo $state > $statefile

	sleep 1

	linkspeed="`cat $linkspeedfile`"
	expected_linkspeed=$((maxstate-state))
	expected_str="${PCIELINKSPEED[$expected_linkspeed]}"
	if [ ! "${expected_str}" = "${linkspeed}" ]; then
		echo "$TESTNAME failed: expected: ${expected_str}; got ${linkspeed}"
		retval=1
	fi
}

cleanup_skip ()
{
	set_state $oldstate
	exit $ksft_skip
}

trap cleanup_skip EXIT

echo "$TESTNAME: testing states $maxstate .. $oldstate with $coolingdev"
for i in $(seq $maxstate -1 $oldstate); do
	set_state "$i"
done

trap EXIT
if [ $retval -eq 0 ]; then
	echo "$TESTNAME [PASS]"
else
	echo "$TESTNAME [FAIL]"
fi
exit $retval
