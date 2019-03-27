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
# ident	"@(#)xattr_006_pos.ksh	1.1	07/02/06 SMI"
#

# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/xattr/xattr_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  xattr_006_pos
#
# DESCRIPTION:
# Xattrs present on a file in a snapshot should be visible.
#
# STRATEGY:
#	1. Create a file and give it an xattr
#       2. Take a snapshot of the filesystem
#	3. Verify that we can take a snapshot of it.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-05)
#
# __stc_assertion_end
#
################################################################################

function cleanup {

	log_must $ZFS destroy $TESTPOOL/$TESTFS@snap
	log_must $RM $TESTDIR/myfile.${TESTCASE_ID}

}

log_assert "read xattr on a snapshot"
log_onexit cleanup

# create a file, and an xattr on it
log_must $TOUCH $TESTDIR/myfile.${TESTCASE_ID}
create_xattr $TESTDIR/myfile.${TESTCASE_ID} passwd /etc/passwd

# snapshot the filesystem
log_must $ZFS snapshot $TESTPOOL/$TESTFS@snap

# check for the xattr on the snapshot
verify_xattr $TESTDIR/$(get_snapdir_name)/snap/myfile.${TESTCASE_ID} passwd /etc/passwd

log_pass "read xattr on a snapshot"
