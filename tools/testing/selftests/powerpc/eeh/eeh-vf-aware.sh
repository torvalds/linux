#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

. ./eeh-functions.sh

eeh_test_prep # NB: may exit

vf_list="$(eeh_enable_vfs)";
if $? != 0 ; then
	log "No usable VFs found. Skipping EEH unaware VF test"
	exit $KSELFTESTS_SKIP;
fi

log "Enabled VFs: $vf_list"

tested=0
passed=0
for vf in $vf_list ; do
	log "Testing $vf"

	if ! eeh_can_recover $vf ; then
		log "Driver for $vf doesn't support error recovery, skipping"
		continue;
	fi

	tested="$((tested + 1))"

	log "Breaking $vf..."
	if ! eeh_one_dev $vf ; then
		log "$vf failed to recover"
		continue;
	fi

	passed="$((passed + 1))"
done

eeh_disable_vfs

if [ "$tested" == 0 ] ; then
	echo "No VFs with EEH aware drivers found, skipping"
	exit $KSELFTESTS_SKIP
fi

test "$failed" != 0
exit $?;
