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
# ident	"@(#)zones_005_pos.ksh	1.1	07/05/25 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  zones_005_pos
#
# DESCRIPTION:
#
# Pool properties can be read but can't be set within a zone
#
# STRATEGY:
# 1. Verify we can read pool properties in a zone
# 2. Verify we can't set a pool property in a zone
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-04-03)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "local"

log_assert "Pool properties can be read but can't be set within a zone"

log_must $ZPOOL get all zonepool
log_must $ZPOOL get bootfs zonepool
log_mustnot $ZPOOL set boofs=zonepool zonepool

# verify that the property hasn't been set.
log_must eval "$ZPOOL get bootfs zonepool > $TMPDIR/output.${TESTCASE_ID}"
log_must $GREP "zonepool  bootfs    -" $TMPDIR/output.${TESTCASE_ID}

$RM $TMPDIR/output.${TESTCASE_ID}

log_pass "Pool properties can be read but can't be set within a zone"
