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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)xattr_005_pos.ksh	1.1	07/02/06 SMI"
#

# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/xattr/xattr_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  xattr_005_pos
#
# DESCRIPTION:
# read/write/create/delete xattr on a clone filesystem
# 
#
# STRATEGY:
#	1. Create an xattr on a filesystem
#	2. Snapshot the filesystem and clone it
#       3. Verify the xattr can still be read, written, deleted
#	4. Verify we can create new xattrs on new files created on the clone
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-13)
#
# __stc_assertion_end
#
################################################################################

function cleanup {

	log_must $ZFS destroy $TESTPOOL/$TESTFS/clone	
	log_must $ZFS destroy $TESTPOOL/$TESTFS@snapshot1
	log_must $RM $TESTDIR/myfile.${TESTCASE_ID}
}

log_assert "read/write/create/delete xattr on a clone filesystem"
log_onexit cleanup

# create a file, and an xattr on it
log_must $TOUCH $TESTDIR/myfile.${TESTCASE_ID}
create_xattr $TESTDIR/myfile.${TESTCASE_ID} passwd /etc/passwd

# snapshot & clone the filesystem
log_must $ZFS snapshot $TESTPOOL/$TESTFS@snapshot1
log_must $ZFS clone $TESTPOOL/$TESTFS@snapshot1 $TESTPOOL/$TESTFS/clone
log_must $ZFS set mountpoint=$TESTDIR/clone $TESTPOOL/$TESTFS/clone

# check for the xattrs on the clone
verify_xattr $TESTDIR/clone/myfile.${TESTCASE_ID} passwd /etc/passwd

# check we can create xattrs on the clone
create_xattr $TESTDIR/clone/myfile.${TESTCASE_ID} foo /etc/passwd
delete_xattr $TESTDIR/clone/myfile.${TESTCASE_ID} foo

# delete the original dataset xattr
delete_xattr $TESTDIR/myfile.${TESTCASE_ID} passwd

# verify it's still there on the clone
verify_xattr $TESTDIR/clone/myfile.${TESTCASE_ID} passwd /etc/passwd
delete_xattr $TESTDIR/clone/myfile.${TESTCASE_ID} passwd

log_pass "read/write/create/delete xattr on a clone filesystem"
