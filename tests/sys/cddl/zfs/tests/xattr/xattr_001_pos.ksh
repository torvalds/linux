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
# ident	"@(#)xattr_001_pos.ksh	1.1	07/02/06 SMI"
#

# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/xattr/xattr_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  xattr_001_pos
#
# DESCRIPTION:
#
# Creating, reading and writing xattrs on ZFS filesystems works as expected
#
# STRATEGY:
#	1. Create an xattr on a ZFS-based file using runat
#	2. Read an empty xattr directory
#       3. Write the xattr using runat and cat
#	3. Read the xattr using runat
#	4. Delete the xattr
#	5. List the xattr namespace successfully, checking for deletion
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

	if [ -f $TESTDIR/myfile.${TESTCASE_ID} ]
	then
		log_must $RM $TESTDIR/myfile.${TESTCASE_ID}
	fi
}

log_assert "Create/read/write/append of xattrs works"
log_onexit cleanup

log_must $TOUCH $TESTDIR/myfile.${TESTCASE_ID}
create_xattr $TESTDIR/myfile.${TESTCASE_ID} passwd /etc/passwd
verify_write_xattr $TESTDIR/myfile.${TESTCASE_ID} passwd
delete_xattr $TESTDIR/myfile.${TESTCASE_ID} passwd

log_pass "Create/read/write of xattrs works"
