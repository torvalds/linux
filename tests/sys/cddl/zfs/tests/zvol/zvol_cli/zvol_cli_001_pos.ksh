#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)zvol_cli_001_pos.ksh	1.2	07/01/09 SMI"
#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_cli_001_pos
#
# DESCRIPTION:
# Executing well-formed 'zfs list' commands should return success
#
# STRATEGY:
# 1. Create an array of valid options.
# 2. Execute each element in the array.
# 3. Verify success is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

set -A args  "list" "list -r" \
    "list $TESTPOOL/$TESTVOL" "list -r $TESTPOOL/$TESTVOL"  \
    "list -H $TESTPOOL/$TESTVOL" "list -Hr $TESTPOOL/$TESTVOL"  \
    "list -rH $TESTPOOL/$TESTVOL" "list -o name $TESTPOOL/$TESTVOL" \
    "list -r -o name $TESTPOOL/$TESTVOL" "list -H -o name $TESTPOOL/$TESTVOL" \
    "list -rH -o name $TESTPOOL/$TESTVOL"

log_assert "Executing well-formed 'zfs list' commands should return success"

typeset -i i=0
while (( $i < ${#args[*]} )); do
	log_must eval "$ZFS ${args[i]} > /dev/null"
	((i = i + 1))
done

log_pass "Executing zfs list on volume works as expected"
