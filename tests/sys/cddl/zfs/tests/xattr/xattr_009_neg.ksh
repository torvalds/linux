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
# ident	"@(#)xattr_009_neg.ksh	1.1	07/02/06 SMI"
#

# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/xattr/xattr_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  xattr_009_neg
#
# DESCRIPTION:
# links between xattr and normal file namespace fail
# 
# STRATEGY:
#	1. Create a file and add an xattr to it (to ensure the namespace exists)
#       2. Verify we're unable to create a symbolic link
#	3. Verify we're unable to create a hard link
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

	log_must $RM $TESTDIR/myfile.${TESTCASE_ID}

}

log_assert "links between xattr and normal file namespace fail"
log_onexit cleanup

test_requires RUNAT

# create a file, and an xattr on it
log_must $TOUCH $TESTDIR/myfile.${TESTCASE_ID}
create_xattr $TESTDIR/myfile.${TESTCASE_ID} passwd /etc/passwd

# Try to create a soft link from the xattr namespace to the default namespace
log_mustnot $RUNAT $TESTDIR/myfile.${TESTCASE_ID} $LN -s /etc/passwd foo

# Try to create a hard link from the xattr namespace to the default namespace
log_mustnot $RUNAT $TESTDIR/myfile.${TESTCASE_ID} $LN /etc/passwd foo

log_pass "links between xattr and normal file namespace fail"
