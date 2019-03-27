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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfs_rename_013_pos.ksh	1.1	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_013_pos
#
# DESCRIPTION:
#	zfs rename -r can rename snapshot when child datasets
#	don't have a snapshot of the given name.
#
# STRATEGY:
#	1. Create snapshot.
#	2. Rename snapshot recursively.
#	3. Verify rename -r snapshot correctly.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-04-24)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

# Check if current system support recursive rename
$ZFS rename 2>&1 | grep "rename -r" > /dev/null 2>&1
if (($? != 0)); then
	log_unsupported
fi

function cleanup
{
	if datasetexists $TESTPOOL/$TESTCTR@snap-new ; then
		log_must $ZFS destroy -f $TESTPOOL/$TESTCTR@snap-new
	fi

	if datasetexists $TESTPOOL/$TESTCTR@snap ; then
		log_must $ZFS destroy -f $TESTPOOL/$TESTCTR@snap
	fi

	if datasetexists $TESTPOOL@snap-new ; then
		log_must $ZFS destroy -f $TESTPOOL@snap-new
	fi

	if datasetexists $TESTPOOL@snap ; then
		log_must $ZFS destroy -f $TESTPOOL@snap
	fi
}

log_assert "zfs rename -r can rename snapshot when child datasets" \
	"don't have a snapshot of the given name."

log_onexit cleanup

log_must $ZFS snapshot $TESTPOOL/$TESTCTR@snap
log_must $ZFS rename -r $TESTPOOL/$TESTCTR@snap $TESTPOOL/$TESTCTR@snap-new
log_must datasetexists $TESTPOOL/$TESTCTR@snap-new

log_must $ZFS snapshot $TESTPOOL@snap
log_must $ZFS rename -r $TESTPOOL@snap $TESTPOOL@snap-new
log_must datasetexists $TESTPOOL/$TESTCTR@snap-new
log_must datasetexists $TESTPOOL@snap-new

log_must $ZFS destroy -f $TESTPOOL/$TESTCTR@snap-new
log_must $ZFS destroy -f $TESTPOOL@snap-new

log_pass "Verify zfs rename -r passed when child datasets" \
	"don't have a snapshot of the given name."

