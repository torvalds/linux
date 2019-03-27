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
# ident	"@(#)rsend_009_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_009_pos
#
# DESCRIPTION:
#	zfs receive can handle out of space correctly.
#
# STRATEGY:
#	1. Create two pools, one is big and another is small.
#	2. Fill the big pool with data.
#	3. Take snapshot and backup the whole pool.
#	4. Receive this stream in small pool.
#	5. Verify zfs receive can handle the out of space error correctly.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-10-10)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if datasetexists bpool ; then
		log_must $ZPOOL destroy -f bpool
	fi
	if datasetexists spool ; then
		log_must $ZPOOL destroy -f spool
	fi
}

log_assert "Verify zfs receive can handle out of space correctly."
log_onexit cleanup

log_must $TRUNCATE -s 100M $TESTDIR/bfile
log_must $TRUNCATE -s 64M  $TESTDIR/sfile
log_must $ZPOOL create bpool $TESTDIR/bfile
log_must $ZPOOL create spool $TESTDIR/sfile

#
# Test out of space on sub-filesystem
#
log_must $ZFS create bpool/fs
mntpnt=$(get_prop mountpoint bpool/fs)
log_must $TRUNCATE -s 30M $mntpnt/file

log_must $ZFS snapshot bpool/fs@snap
log_must eval "$ZFS send -R bpool/fs@snap > $BACKDIR/fs-R"
log_mustnot eval "$ZFS receive -d -F spool < $BACKDIR/fs-R"

log_must datasetnonexists spool/fs
log_must ismounted spool

#
# Test out of space on top filesystem
#
mntpnt2=$(get_prop mountpoint bpool)
log_must $MV $mntpnt/file $mntpnt2
log_must $ZFS destroy -rf bpool/fs

log_must $ZFS snapshot bpool@snap
log_must eval "$ZFS send -R bpool@snap > $BACKDIR/bpool-R"
log_mustnot eval "$ZFS receive -d -F spool < $BACKDIR/bpool-R"

log_must datasetnonexists spool/fs
log_must ismounted spool

log_pass "zfs receive can handle out of space correctly."
