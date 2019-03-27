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
# ident	"@(#)zfs_receive_004_neg.ksh	1.4	07/10/09 SMI"
#

. $STF_SUITE/tests/cli_root/cli_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_receive_004_neg
#
# DESCRIPTION:
#	Verify 'zfs receive' fails with malformed parameters.
#
# STRATEGY:
#	1. Denfine malformed parameters array
#	2. Feed the malformed parameters to 'zfs receive' 
#	3. Verify the command should be failed
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

	for snap in $init_snap $inc_snap $init_topsnap $inc_topsnap ; do
		snapexists $snap && \
			log_must $ZFS destroy -Rf $snap
	done

	for bkup in $full_bkup $inc_bkup $full_topbkup $inc_topbkup; do
		[[ -e $bkup ]] && \
			log_must $RM -f $bkup
	done
}

log_assert "Verify that invalid parameters to 'zfs receive' are caught." 
log_onexit cleanup

init_snap=$TESTPOOL/$TESTFS@initsnap
inc_snap=$TESTPOOL/$TESTFS@incsnap
full_bkup=$TMPDIR/full_bkup.${TESTCASE_ID}
inc_bkup=$TMPDIR/inc_bkup.${TESTCASE_ID}

init_topsnap=$TESTPOOL@initsnap
inc_topsnap=$TESTPOOL@incsnap
full_topbkup=$TMPDIR/full_topbkup.${TESTCASE_ID}
inc_topbkup=$TMPDIR/inc_topbkup.${TESTCASE_ID}

log_must $ZFS snapshot $init_topsnap
log_must eval "$ZFS send $init_topsnap > $full_topbkup"

log_must $ZFS snapshot $inc_topsnap
log_must eval "$ZFS send -i $init_topsnap $inc_topsnap > $inc_topbkup"

log_must $ZFS snapshot $init_snap
log_must eval "$ZFS send $init_snap > $full_bkup"

log_must $ZFS snapshot $inc_snap
log_must eval "$ZFS send -i $init_snap $inc_snap > $inc_bkup"

set -A badargs \
	"" "nonexistent-snap" "blah@blah" "$snap1" "$snap1 $snap2" \
	"-d" "-d nonexistent-dataset" \
	"$TESTPOOL/fs@" "$TESTPOOL/fs@@mysnap" "$TESTPOOL/fs@@" \
	"$TESTPOOL/fs/@mysnap" "$TESTPOOL/fs@/mysnap" \
	"$TESTPOOL/nonexistent-fs/nonexistent-fs" \
	"-d $TESTPOOL/nonexistent-fs" "-d $TESTPOOL/$TESTFS/nonexistent-fs"

typeset -i i=0
while (( i < ${#badargs[*]} ))
do
	for bkup in $full_bkup $inc_bkup $full_topbkup $inc_topbkup ; do
		log_mustnot eval "$ZFS receive ${badargs[i]} < $bkup"
	done
	
	(( i = i + 1 ))
done

log_pass "Invalid parameters to 'zfs receive' are caught as expected."
