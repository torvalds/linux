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
# ident	"@(#)zfs_receive_005_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/cli_root/cli_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_receive_005_neg
#
# DESCRIPTION:
#	Verify 'zfs receive' fails with unsupported scenarios.
#	including:
#	(1) Invalid send streams;
#	(2) The received incremental send doesn't match the filesystem
#	    latest status.
#
# STRATEGY:
#	1. Preparation for unsupported scenarios 
#	2. Execute 'zfs receive' 
#	3. Verify the results are failed
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-06)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset snap
	typeset bkup

	for snap in $init_snap $inc_snap; do
		snapexists $snap && \
			log_must $ZFS destroy -f $snap
	done

	datasetexists $rst_root && \
		log_must $ZFS destroy -Rf $rst_root
	
	for bkup in $full_bkup $inc_bkup; do
		[[ -e $bkup ]] && \
			log_must $RM -f $bkup
	done
}

log_assert "Verify 'zfs receive' fails with unsupported scenarios." 
log_onexit cleanup

init_snap=$TESTPOOL/$TESTFS@initsnap
inc_snap=$TESTPOOL/$TESTFS@incsnap
rst_root=$TESTPOOL/rst_ctr
rst_init_snap=$rst_root/$TESTFS@init_snap
rst_inc_snap=$rst_root/$TESTFS@inc_snap
full_bkup=$TMPDIR/full_bkup.${TESTCASE_ID}
inc_bkup=$TMPDIR/inc_bkup.${TESTCASE_ID}

log_must $ZFS create $rst_root
log_must $ZFS snapshot $init_snap
log_must eval "$ZFS send $init_snap > $full_bkup"

log_note "'zfs receive' fails with invalid send streams."
log_mustnot eval "$ZFS receive $rst_init_snap < /dev/zero"
log_mustnot eval "$ZFS receive -d $rst_root </dev/zero"

log_must eval "$ZFS receive $rst_init_snap < $full_bkup"

log_note "Unmatched send stream with restoring filesystem" \
	" cannot be received."
log_must $ZFS snapshot $inc_snap
log_must eval "$ZFS send -i $init_snap $inc_snap > $inc_bkup"
#make changes on the restoring filesystem
log_must $TOUCH $ZFSROOT/$rst_root/$TESTFS/tmpfile
log_mustnot eval "$ZFS receive $rst_inc_snap < $inc_bkup"
log_mustnot eval "$ZFS receive -d $rst_root < $inc_bkup"

log_pass "Unsupported scenarios to 'zfs receive' fail as expected."
