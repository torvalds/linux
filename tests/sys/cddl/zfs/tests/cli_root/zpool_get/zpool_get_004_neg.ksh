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
# ident	"@(#)zpool_get_004_neg.ksh	1.3	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  zpool_get_004_neg
#
# DESCRIPTION:
#
# Malformed zpool get commands are rejected
#
# STRATEGY:
#
# 1. Run several different "zpool get" commands that should fail.
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

log_assert "Malformed zpool get commands are rejected"

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

set -A arguments "$TESTPOOL $TESTPOOL" "$TESTPOOL rubbish" "-v $TESTPOOL" \
		"nosuchproperty $TESTPOOL" "--$TESTPOOL" "all all" \
		"type $TESTPOOL" "usage: $TESTPOOL" "bootfs $TESTPOOL@" \
		"bootfs,bootfs $TESTPOOL" "name $TESTPOOL" "t%d%s" \
		"bootfs,delegation $TESTPOOL" "delegation $TESTPOOL@"

for arg in $arguments
do
	log_mustnot $ZPOOL get $arg
done

log_pass "Malformed zpool get commands are rejected"
