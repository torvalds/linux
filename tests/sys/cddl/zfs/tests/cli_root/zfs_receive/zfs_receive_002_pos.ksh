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
# ident	"@(#)zfs_receive_002_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/cli_root/cli_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_receive_002_pos
#
# DESCRIPTION:
#	Verifying 'zfs receive <volume>' works.
#
# STRATEGY:
#	1. Fill in volume with some data
#	2. Create full and incremental send stream
#	3. Restore the send stream  
#	4. Verify the restoring results.
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

verify_runnable "global"

function cleanup
{
	typeset -i i=0
	typeset ds
	
	while (( i < ${#orig_snap[*]} )); do
		snapexists ${rst_snap[$i]} && \
			log_must $ZFS destroy -f ${rst_snap[$i]}
		snapexists ${orig_snap[$i]} && \
			log_must $ZFS destroy -f ${orig_snap[$i]}
		[[ -e ${bkup[$i]} ]] && \
			log_must $RM -rf ${bkup[$i]}

		(( i = i + 1 ))
	done

	for ds in $rst_vol $rst_root; do
		datasetexists $ds && \
			log_must $ZFS destroy -Rf $ds
	done
}

log_assert "Verifying 'zfs receive <volume>' works."
log_onexit cleanup

set -A orig_snap "$TESTPOOL/$TESTVOL@init_snap" "$TESTPOOL/$TESTVOL@inc_snap"
set -A bkup "$TMPDIR/fullbkup" "$TMPDIR/incbkup"
rst_root=$TESTPOOL/rst_ctr
rst_vol=$rst_root/$TESTVOL
set -A rst_snap "${rst_vol}@init_snap" "${rst_vol}@inc_snap"

#
# Preparations for testing
# 
log_must $ZFS create $rst_root
[[ ! -d $TESTDIR1 ]] && \
	log_must $MKDIR -p $TESTDIR1
log_must $ZFS set mountpoint=$TESTDIR1 $rst_root

typeset -i i=0
while (( i < ${#orig_snap[*]} )); do
	log_must $ZFS snapshot ${orig_snap[$i]}
	if (( i < 1 )); then
		log_must eval "$ZFS send ${orig_snap[$i]} > ${bkup[$i]}"
	else
		log_must eval "$ZFS send -i ${orig_snap[(( i - 1 ))]} \
				${orig_snap[$i]} > ${bkup[$i]}"
	fi

	(( i = i + 1 ))
done
	
i=0
while (( i < ${#bkup[*]} )); do
	log_must eval "$ZFS receive $rst_vol < ${bkup[$i]}"
	! datasetexists $rst_vol || ! snapexists ${rst_snap[$i]} && \
		log_fail "Restoring volume fails."

	(( i = i + 1 ))
done

log_pass "Verifying 'zfs receive <volume>' succeeds."
