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
# ident	"@(#)rename_dirs_001_pos.ksh	1.1	07/05/25 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###########################################################################
#
# __stc_assertion_start
#
# ID: rename_dirs_001_pos
#
# DESCRIPTION:
# Create two directory trees in ZFS filesystem, and concurently rename
# directory across the two trees. ZFS should be able to handle the race
# situation.
#
# STRATEGY:
# 1. Create a ZFS filesystem
# 2. Make two directory tree in the zfs file system
# 3. Continually rename directory from one tree to another tree in two process
# 4. After the specified time duration, the system should not be panic.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-02-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	log_must $RM -rf $TESTDIR/*
}

log_assert "ZFS can handle race directory rename operation."

log_onexit cleanup

$CD $TESTDIR
$MKDIR -p 1/2/3/4/5 a/b/c/d/e

$RENAME_DIR &

$SLEEP $WAITTIME
typeset -i retval=1
$PGREP $RENAME_DIR >/dev/null 2>&1
retval=$?
if (( $retval == 0 )); then
	$PKILL -9 $RENAME_DIR >/dev/null 2>&1
fi

log_pass "ZFS handle race directory rename operation as expected."
