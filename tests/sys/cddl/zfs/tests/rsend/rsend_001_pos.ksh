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
# ident	"@(#)rsend_001_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_001_pos
#
# DESCRIPTION:
#	zfs send -R send replication stream up to the named snap.
#
# STRATEGY:
#	1. Back up all the data from POOL/FS
#	2. Verify all the datasets and data can be recovered in POOL2
#	3. Back up all the data from root filesystem POOL2
#	4. Verify all the data can be recovered, too
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "zfs send -R send replication stream up to the named snap."
log_onexit cleanup_pool $POOL2

#
# Verify the entire pool and sub-ds can be backup and restored.
#
log_must eval "$ZFS send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-final-R"

dstds=$(get_dst_ds $POOL $POOL2)
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

# Cleanup POOL2
log_must cleanup_pool $POOL2

#
# Verify all the filesystem and sub-fs can be backup and restored.
#
log_must eval "$ZFS send -R $POOL/$FS@final > $BACKDIR/fs-final-R"
log_must eval "$ZFS receive -d $POOL2 < $BACKDIR/fs-final-R"

dstds=$(get_dst_ds $POOL/$FS $POOL2)
log_must cmp_ds_subs $POOL/$FS $dstds
log_must cmp_ds_cont $POOL/$FS $dstds

log_pass "zfs send -R send replication stream up to the named snap."
