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
# ident	"@(#)history_010_pos.ksh	1.4	09/01/12 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_010_pos
#
# DESCRIPTION:
#	Verify internal long history information are correct.
#
# STRATEGY:
#	1. Create non-root test user and group.
#	2. Do some zfs operation test by root and non-root user.
#	3. Verify the long history information are correct.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

$ZFS 2>&1 | $GREP "allow" > /dev/null
(($? != 0)) && log_unsupported

function cleanup
{
	[[ -f $REAL_HISTORY ]] && $RM -f $REAL_HISTORY	
	[[ -f $EXPECT_HISTORY ]] && $RM -f $EXPECT_HISTORY

	del_user $HIST_USER
	del_group $HIST_GROUP

	datasetexists $root_testfs && log_must $ZFS destroy -rf $root_testfs
}

log_assert "Verify internal long history information are correct."
log_onexit cleanup

root_testfs=$TESTPOOL/$TESTFS1

# Create history test group and user and get user id and group id
add_group $HIST_GROUP
add_user $HIST_GROUP $HIST_USER
uid=$($ID $HIST_USER | $AWK -F= '{print $2}'| $AWK -F"(" '{print $1}' )
gid=$($ID $HIST_USER | $AWK -F= '{print $3}'| $AWK -F"(" '{print $1}' )

# Get original long history
format_history $TESTPOOL $EXPECT_HISTORY "-l"

exec_record -l $ZFS create $root_testfs
exec_record -l $ZFS allow $HIST_GROUP snapshot,mount $root_testfs
exec_record -l $ZFS allow $HIST_USER destroy,mount $root_testfs
exec_record -l $ZFS allow $HIST_USER reservation $root_testfs
exec_record -l $ZFS allow $HIST_USER allow $root_testfs

exec_record -l -u $HIST_USER "$ZFS snapshot $root_testfs@snap"
exec_record -l -u $HIST_USER "$ZFS destroy $root_testfs@snap"
exec_record -l -u $HIST_USER "$ZFS reservation=64M $root_testfs"
exec_record -l -u $HIST_USER "$ZFS allow $HIST_USER reservation $root_testfs"
exec_record -l $ZFS unallow $HIST_USER create $root_testfs
exec_record -l $ZFS unallow $HIST_GROUP snapshot $root_testfs
exec_record -l $ZFS destroy -r $root_testfs

format_history $TESTPOOL $REAL_HISTORY "-l"
log_must $DIFF $REAL_HISTORY $EXPECT_HISTORY

del_user $HIST_USER
del_group $HIST_GROUP

log_pass "Verify internal long history information pass."
