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
# ident	"@(#)zfs_share_006_pos.ksh	1.3	07/02/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_share_006_pos
#
# DESCRIPTION:
# Verify that a dataset could not be shared but filesystems are shared.
#
# STRATEGY:
# 1. Create a dataset and file system
# 2. Set the sharenfs property on the dataset
# 3. Verify that the dataset is unable be shared.
# 4. Add a new file system to the dataset.
# 5. Verify that the newly added file system be shared.
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

verify_runnable "global"

function cleanup
{
	log_must $ZFS set sharenfs=off $TESTPOOL/$TESTCTR	
	if mounted $TESTDIR2; then
		log_must $ZFS unmount $TESTDIR2
	fi

	datasetexists $TESTPOOL/$TESTCTR/$TESTFS2 && \
		log_must $ZFS destroy $TESTPOOL/$TESTCTR/$TESTFS2

	typeset fs=""
	for fs in $mntp $TESTDIR1 $TESTDIR2
	do
		log_must unshare_fs $fs
	done
}

#
# Main test routine.
#
# Given a mountpoint and a dataset, this routine will set the
# sharenfs property on the dataset and verify that dataset
# is unable to be shared but the existing contained file systems 
# could be shared.
#
function test_ctr_share # mntp ctr
{
	typeset mntp=$1
	typeset ctr=$2

	not_shared $mntp || \
	    log_fail "Mountpoint: $mntp is already shared."

	log_must $ZFS set sharenfs=on $ctr

	not_shared $mntp || \
		log_fail "File system $mntp is shared (set sharenfs)."

	#
	# Add a new file system to the dataset and verify it is shared.
	#
	typeset mntp2=$TESTDIR2
	log_must $ZFS create $ctr/$TESTFS2
	log_must $ZFS set mountpoint=$mntp2 $ctr/$TESTFS2

	is_shared $mntp2 || \
	    log_fail "File system $mntp2 was not shared (set sharenfs)."
}

log_assert "Verify that a dataset could not be shared, " \
	"but its sub-filesystems could be shared."
log_onexit cleanup

typeset mntp=$(get_prop mountpoint $TESTPOOL/$TESTCTR)
test_ctr_share $mntp $TESTPOOL/$TESTCTR

log_pass "A dataset could not be shared, " \
	"but its sub-filesystems could be shared as expect."
