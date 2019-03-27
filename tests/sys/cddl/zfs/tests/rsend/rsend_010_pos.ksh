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
# ident	"@(#)rsend_010_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_010_pos
#
# DESCRIPTION:
#	ZFS can handle stream with multiple identical (same GUID) snapshots
#
# STRATEGY:
#	1. Recursively backup snapshot
#	2. Restore it to the given filesystem
#	3. Resend the snapshot again
#	4. Verify this stream can be restore to this filesystem again
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-10-11)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "ZFS can handle stream with multiple identical (same GUID) snapshots"
log_onexit cleanup_pool $POOL2

log_must $ZFS create $POOL2/$FS
log_must $ZFS snapshot $POOL2/$FS@snap

#
# First round restore the stream
#
log_must eval "$ZFS send -R $POOL2/$FS@snap > $BACKDIR/fs-R"
log_must eval "$ZFS receive -d -F $POOL2/$FS < $BACKDIR/fs-R"

#
# In order to avoid 'zfs send -R' failed, create snapshot for 
# all the sub-systems
#
list=$($ZFS list -r -H -o name -t filesystem $POOL2/$FS)
for item in $list ; do
	if datasetnonexists $item@snap ; then
		log_must $ZFS snapshot $item@snap
	fi
done

#
# Second round restore the stream
#
log_must eval "$ZFS send -R $POOL2/$FS@snap > $BACKDIR/fs-R"
dstds=$(get_dst_ds $POOL2/$FS $POOL2/$FS)
log_must eval "$ZFS receive -d -F $dstds < $BACKDIR/fs-R"

log_pass "ZFS can handle stream with multiple identical (same GUID) snapshots"
