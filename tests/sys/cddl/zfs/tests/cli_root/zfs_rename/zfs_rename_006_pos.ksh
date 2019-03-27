#!/usr/local/bin/ksh93 
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
# ident	"@(#)zfs_rename_006_pos.ksh	1.3	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_rename/zfs_rename.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_006_pos
#
# DESCRIPTION: 
#       'zfs rename' can successfully rename a volume snapshot.
#
# STRATEGY:
#       1. Create a snapshot of volume.
#       2. Rename volume snapshot to a new one.
#	3. Rename volume to a new one.
#       5. Verify that the rename operations are successful and zfs list can
#	   list them.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-02-05)
#
# __stc_assertion_end
#
############################################################################### 

verify_runnable "global"

#
# cleanup defined in zfs_rename.kshlib
#
log_onexit cleanup

log_assert "'zfs rename' can successfully rename a volume snapshot."

vol=$TESTPOOL/$TESTVOL
snap=$TESTSNAP

log_must eval "$DD if=$DATA of=$VOL_R_PATH bs=$BS count=$CNT >/dev/null 2>&1"
if ! snapexists $vol@$snap; then
	log_must $ZFS snapshot $vol@$snap
fi

rename_dataset $vol@$snap $vol@${snap}-new
rename_dataset $vol ${vol}-new
rename_dataset ${vol}-new@${snap}-new ${vol}-new@$snap
rename_dataset ${vol}-new $vol

#verify data integrity
for input in $VOL_R_PATH ${VOL_R_PATH}@$snap; do
	log_must eval "$DD if=$input of=$VOLDATA bs=$BS count=$CNT >/dev/null 2>&1"
	if ! cmp_data $VOLDATA $DATA ; then
		log_fail "$input gets corrupted after rename operation."
	fi
done

log_must $ZFS destroy $vol@$snap

log_pass "'zfs rename' can rename volume snapshot as expected."
