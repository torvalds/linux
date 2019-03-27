#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)snapshot_014_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_014_pos
#
# DESCRIPTION:
#	verify that creating/destroying snapshots do things clean
#
# STRATEGY:
#	1. create a dataset and set a quota with 500m
#	2. create file of size 400m on the dataset	
#	3. take a snapshot and destroy it
#	4. then create file to use all spaces in the dataset
#	5. verify removing the first file should succeed
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-10-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	[[ -e $TESTDIR1 ]] && \
		log_must $RM -rf $TESTDIR1/* > /dev/null 2>&1

	snapexists $SNAPCTR && \
		log_must $ZFS destroy $SNAPCTR

	datasetexists $TESTPOOL/$TESTCTR/$TESTFS1 && \
		log_must $ZFS set quota=none $TESTPOOL/$TESTCTR/$TESTFS1

}

log_assert "Verify creating/destroying snapshots do things clean"
log_onexit cleanup

log_must $ZFS set quota=$FSQUOTA $TESTPOOL/$TESTCTR/$TESTFS1
log_must $MKFILE $FILESIZE $TESTDIR1/$TESTFILE

log_must $ZFS snapshot $SNAPCTR
log_must $ZFS destroy $SNAPCTR

log_note "Make the quota of filesystem is reached"
log_mustnot $MKFILE $FILESIZE1 $TESTDIR1/$TESTFILE1

log_note "Verify removing the first file should succeed after the snapshot is \
	removed"
log_must $RM $TESTDIR1/$TESTFILE

log_pass "Verify creating/destroying snapshots do things clean"
