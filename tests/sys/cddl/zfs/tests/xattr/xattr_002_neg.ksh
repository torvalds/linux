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
# ident	"@(#)xattr_002_neg.ksh	1.1	07/02/06 SMI"
#

# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/xattr/xattr_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  xattr_002_neg
#
# DESCRIPTION:
#
# Trying to read a non-existent xattr should fail.
#
# STRATEGY:
#	1. Create a file
#       2. Try to read a non-existent xattr, check that an error is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-12-05)
#
# __stc_assertion_end
#
################################################################################

function cleanup {

	log_must $RM $TESTDIR/myfile.${TESTCASE_ID}

}

log_assert "A read of a non-existent xattr fails"
log_onexit cleanup

# create a file
log_must $TOUCH $TESTDIR/myfile.${TESTCASE_ID}
log_mustnot eval "$CAT $TESTDIR/myfile.${TESTCASE_ID} not-here.txt > /dev/null 2>&1"

log_pass "A read of a non-existent xattr fails"
