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
# ident	"@(#)share_mount_001_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: share_mount_001_neg
#
# DESCRIPTION:
# Verify that we cannot share or mount legacy filesystems.
#
# STRATEGY:
# 1. Set mountpoint as legacy or none
# 2. Use zfs share or zfs mount to share or mount the filesystem
# 3. Verify that the command returns error
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-9)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	log_must $ZFS set mountpoint=$oldmpt $fs
}

log_assert "Verify that we cannot share or mount legacy filesystems."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
oldmpt=$(get_prop mountpoint $fs)

for propval in "legacy" "none"; do 
	log_must $ZFS set mountpoint=$propval $fs
	
	log_mustnot $ZFS mount $fs
	log_mustnot $ZFS share $fs
done

log_pass "We cannot share or mount legacy filesystems as expected."
