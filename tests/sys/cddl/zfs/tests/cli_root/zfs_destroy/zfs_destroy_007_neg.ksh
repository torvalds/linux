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
# ident	"@(#)zfs_destroy_007_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_destroy_007_neg
#
# DESCRIPTION:
#	'zpool destroy' failed if this filesystem is namespace-parent
#	of origin. 
#
# STRATEGY:
#	1. Create pool, fs and snapshot.
#	2. Create a namespace-parent of origin clone.
#	3. Promote this clone
#	4. Verify the original fs can not be destroyed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-18)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if datasetexists $clonesnap; then
		log_must $ZFS promote $fs
	fi
	datasetexists $clone && log_must $ZFS destroy $clone
	datasetexists $fssnap && log_must $ZFS destroy $fssnap
}

log_assert "Destroy dataset which is namespace-parent of origin should failed."
log_onexit cleanup

# Define variable $fssnap & and namespace-parent of origin clone.
fs=$TESTPOOL/$TESTFS
fssnap=$fs@snap
clone=$fs/clone
clonesnap=$fs/clone@snap

log_must $ZFS snapshot $fssnap
log_must $ZFS clone $fssnap $clone
log_must $ZFS promote $clone
log_mustnot $ZFS destroy $fs
log_mustnot $ZFS destroy $clone

log_pass "Destroy dataset which is namespace-parent of origin passed."
