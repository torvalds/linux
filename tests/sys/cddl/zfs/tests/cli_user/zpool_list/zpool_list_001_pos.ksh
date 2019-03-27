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
# ident	"@(#)zpool_list_001_pos.ksh	1.5	08/05/14 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_list_001_pos
#
# DESCRIPTION:
# Verify that 'zpool list' succeeds as non-root.
#
# STRATEGY:
# 1. Create an array of options.
# 2. Execute each element of the array.
# 3. Verify the command succeeds.
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

verify_runnable "both"

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

set -A args "list $TESTPOOL" "list -H $TESTPOOL" "list" "list -H" \
	"list -H -o name $TESTPOOL" "list -o name $TESTPOOL" \
	"list -o name,size,capacity,health,altroot $TESTPOOL" \
	"list -H -o name,size,capacity,health,altroot $TESTPOOL"

log_assert "zpool list [-H] [-o filed[,filed]*] [<pool_name> ...]"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_must run_unprivileged $ZPOOL ${args[i]}

	((i = i + 1))
done

log_pass "The sub-command 'list' succeeds as non-root."
