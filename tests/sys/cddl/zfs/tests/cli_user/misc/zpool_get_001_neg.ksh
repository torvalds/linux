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
# ident	"@(#)zpool_get_001_neg.ksh	1.2	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_get_001_neg
#
# DESCRIPTION:
#
# zpool get works when run as a user
#
# STRATEGY:
#
# 1. For each property, get that property
# 2. Verify the property was the same as that set in setup
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "zpool get works when run as a user"

set -A props $POOL_PROPS
set -A prop_vals $POOL_VALS

while [[ $i -lt ${#args[*]} ]]
do	
	PROP=${props[$i]}
	EXPECTED=${prop_vals[$i]}
	ACTUAL=$( $ZPOOL get $PROP $TESTPOOL | $GREP $PROP | $AWK '{print $1}' )
	if [ "$ACTUAL" != "$EXPECTED" ]
	then
		log_fail "Property $PROP value was $ACTUAL, expected $EXPECTED"
	fi
        i=$(( $i + 1 ))
done

log_must run_unprivileged "$ZPOOL get all $TESTPOOL"

log_pass "zpool get works when run as a user"
