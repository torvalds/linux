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
# ident	"@(#)zfs_mount_011_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_mount_011_neg
#
# DESCRIPTION:
# Verify that zfs mount should fail with bad parameters
#
# STRATEGY:
# 1. Make an array of bad parameters
# 2. Use zfs mount to mount the filesystem
# 3. Verify that zfs mount returns error
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
	if snapexists $TESTPOOL/$TESTFS@$TESTSNAP; then
		log_must $ZFS destroy $TESTPOOL/$TESTFS@$TESTSNAP
	fi
	
	if is_global_zone && datasetexists $TESTPOOL/$TESTVOL; then
		log_must $ZFS destroy $TESTPOOL/$TESTVOL
	fi
}

log_assert "zfs mount fails with bad parameters"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
set -A badargs "A" "-A" "-" "-x" "-?" "=" "-o *" "-a" 

for arg in "${badargs[@]}"; do
	log_mustnot eval "$ZFS mount $arg $fs >/dev/null 2>&1" 
done 

#verify that zfs mount fails with invalid dataset
for opt in "-o abc" "-O"; do
	log_mustnot eval "$ZFS mount $opt /$fs >/dev/null 2>&1"
done

#verify that zfs mount fails with volume and snapshot
log_must $ZFS snapshot $TESTPOOL/$TESTFS@$TESTSNAP
log_mustnot eval "$ZFS mount $TESTPOOL/$TESTFS@$TESTSNAP >/dev/null 2>&1"

if is_global_zone; then
	log_must $ZFS create -V 10m $TESTPOOL/$TESTVOL
	log_mustnot eval "$ZFS mount $TESTPOOL/$TESTVOL >/dev/null 2>&1"
fi

log_pass "zfs mount fails with bad parameters as expected."
