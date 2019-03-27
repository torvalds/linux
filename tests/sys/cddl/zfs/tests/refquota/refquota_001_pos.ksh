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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)refquota_001_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: refquota_001_pos
#
# DESCRIPTION:
#	refquota limits the amount of space a dataset can consume, but does
#	not include space used by descendents.
#
# STRATEGY:
#	1. Setting refquota in given filesystem
#	2. Create descendent filesystem
#	3. Verify refquota limits the amount of space a dataset can consume
#	4. Verify the limit does not impact descendents
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-12-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "refquota limits the amount of space a dataset can consume, " \
	"but does not include space used by descendents."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
sub=$fs/sub
log_must $ZFS create $sub

log_must $ZFS set refquota=10M $fs
mntpnt=$(get_prop mountpoint $fs)

log_mustnot $MKFILE 11M $mntpnt/file
log_must $MKFILE 9M $mntpnt/file
log_must $ZFS snapshot $fs@snap
log_mustnot $MKFILE 2M $mntpnt/file2

mntpnt=$(get_prop mountpoint $sub)
log_must $MKFILE 10M $mntpnt/file
log_must $ZFS snapshot $sub@snap
log_must $MKFILE 10 $mntpnt/file2

log_pass "refquota limits the amount of space a dataset can consume, " \
	"but does not include space used by descendents."
