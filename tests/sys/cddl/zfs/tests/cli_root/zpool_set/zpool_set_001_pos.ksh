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
# ident	"@(#)zpool_set_001_pos.ksh	1.3	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
################################################################################
#
# __stc_assertion_start
#
# ID:  zpool_set_001_pos
#
# DESCRIPTION:
#
# Zpool set usage message is displayed when called with no arguments
#
# STRATEGY:
#	1. Run zpool set
#	2. Check that exit status is set to 2
#	3. Check usage message contains text "usage"
#
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-03-05)
#
# __stc_assertion_end
#
################################################################################

log_assert "zpool set usage message is displayed when called with no arguments"

$ZPOOL upgrade -v 2>&1 | $GREP "bootfs pool property" > /dev/null
if [ $? -ne 0 ]
then
	log_unsupported "Pool properties not supported on this release."
fi

$ZPOOL set > /dev/null 2>&1 
RET=$?
if [ $RET != 2 ]
then
	log_fail "\"zpool set\" exit status $RET should be equal to 2."
fi

OUTPUT=$($ZPOOL set 2>&1 | $GREP -i usage)
if [ $? != 0 ]
then
	log_fail "Usage message for zpool set did not contain the word 'usage'."
fi

log_pass "zpool set usage message is displayed when called with no arguments"
