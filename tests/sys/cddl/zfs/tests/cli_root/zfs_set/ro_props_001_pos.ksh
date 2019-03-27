#!/usr/local/bin/ksh93 -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# $FreeBSD$

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)ro_props_001_pos.ksh	1.4	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: ro_props_001_pos
#
# DESCRIPTION:
# Verify that read-only properties are immutable.
#
# STRATEGY:
# 1. Create pool, fs, vol, fs@snap & vol@snap.
# 2. Get the original property value and set value to those properties.
# 3. Check return value.
# 4. Compare the current property value with the original one.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

set -A values filesystem volume snapshot -3 0 1 50K 10G 80G \
	2005/06/17 30K 20x yes no \
	on off default pool/fs@snap $TESTDIR 
set -A dataset $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTCTR/$TESTFS1 $TESTPOOL/$TESTFS@$TESTSNAP \
	$TESTPOOL/$TESTVOL@$TESTSNAP

typeset ro_props="type"
ro_props="$ro_props creation"
ro_props="$ro_props compressratio"
ro_props="$ro_props mounted"
ro_props="$ro_props origin"
# Uncomment these once the test ensures they can't be changed.
#ro_props="$ro_props used"
#ro_props="$ro_props available"
#ro_props="$ro_props avail"
#ro_props="$ro_props referenced"
#ro_props="$ro_props refer"

typeset snap_ro_props="volsize"
snap_ro_props="$snap_ro_props recordsize"
snap_ro_props="$snap_ro_props recsize"
snap_ro_props="$snap_ro_props quota"
snap_ro_props="$snap_ro_props reservation"
snap_ro_props="$snap_ro_props reserv"
snap_ro_props="$snap_ro_props mountpoint"
snap_ro_props="$snap_ro_props sharenfs"
snap_ro_props="$snap_ro_props checksum"
snap_ro_props="$snap_ro_props compression"
snap_ro_props="$snap_ro_props compress"
snap_ro_props="$snap_ro_props atime"
snap_ro_props="$snap_ro_props devices"
snap_ro_props="$snap_ro_props exec"
snap_ro_props="$snap_ro_props readonly"
snap_ro_props="$snap_ro_props rdonly"
snap_ro_props="$snap_ro_props setuid"

$ZFS upgrade -v > /dev/null 2>&1
if [[ $? -eq 0 ]]; then
	snap_ro_props="$snap_ro_props version"
fi
	
function cleanup
{
	poolexists $TESTPOOL && log_must $ZPOOL history $TESTPOOL
	datasetexists $TESTPOOL/$TESTVOL@$TESTSNAP && \
		destroy_snapshot $TESTPOOL/$TESTVOL@$TESTSNAP
	datasetexists $TESTPOOL/$TESTFS@$TESTSNAP && \
		destroy_snapshot $TESTPOOL/$TESTFS@$TESTSNAP
}

log_assert "Verify that read-only properties are immutable."
log_onexit cleanup

# Create filesystem and volume's snapshot
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTVOL $TESTSNAP

# Make sure any history logs have been synced.  They're asynchronously
# pushed to the syncing context, and could influence the value of some
# properties on $TESTPOOL, like 'used'.  Fetching it here forces the sync,
# per spa_history.c:spa_history_get().
log_must $ZPOOL history $TESTPOOL

typeset -i i=0
typeset -i j=0
typeset cur_value=""
typeset props=""

while (( i < ${#dataset[@]} )); do
	props=$ro_props

	dst_type=$(get_prop type ${dataset[i]})
	if [[ $dst_type == 'snapshot' ]]; then
		props="$ro_props $snap_ro_props"
	fi

	for prop in $props; do
		cur_value=$(get_prop $prop ${dataset[i]})

		j=0
		while (( j < ${#values[@]} )); do
			#
			# If the current property value is equal to values[j],
			# just expect it failed. Otherwise, set it to dataset,
			# expecting it failed and the property value is not
			# equal to values[j].
			#
			if [[ $cur_value == ${values[j]} ]]; then
				log_mustnot $ZFS set $prop=${values[j]} \
					${dataset[i]}
			else
				set_n_check_prop ${values[j]} $prop \
					${dataset[i]} false
			fi
			(( j += 1 ))
		done
	done
	(( i += 1 ))
done

log_pass "Setting uneditable properties fail, as required."
