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
# ident	"@(#)zpool_export_002_pos.ksh	1.3	09/01/12 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_export_002_pos
#
# DESCRIPTION:
# The 'zpool export' command must fail when a pool is
# busy i.e. mounted.
#
# STRATEGY:
# 1. Try and export the default pool when mounted and busy.
# 2. Verify an error is returned.
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
	typeset dir=$(get_device_dir $DISKS)
	cd $olddir || \
	    log_fail "Couldn't cd back to $olddir"

	datasetexists "$TESTPOOL/$TESTFS" || \
	    log_must $ZPOOL import -d $dir $TESTPOOL

	ismounted "$TESTPOOL/$TESTFS"
	(( $? != 0 )) && \
	    log_must $ZFS mount $TESTPOOL/$TESTFS
	
	[[ -e $TESTDIR/$TESTFILE0 ]] && \
	    log_must $RM -rf $TESTDIR/$TESTFILE0
}

olddir=$PWD

log_onexit cleanup

log_assert "Verify a busy ZPOOL cannot be exported."

ismounted "$TESTPOOL/$TESTFS"
(( $? != 0 )) && \
    log_fail "$TESTDIR not mounted. Unable to continue."

cd $TESTDIR || \
    log_fail "Couldn't cd to $TESTDIR"

log_mustnot $ZPOOL export $TESTPOOL

poolexists $TESTPOOL || \
	log_fail "$TESTPOOL not found in 'zpool list' output."

log_pass "Unable to export a busy ZPOOL as expected."
