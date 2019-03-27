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
# ident	"@(#)zpool_create_003_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_create/zpool_create.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_003_pos
#
# DESCRIPTION:
# 'zpool create -n <pool> <vspec> ...' can display the configuration without
# actually creating the pool.
#
# STRATEGY:
# 1. Create storage pool with -n option
# 2. Verify the pool has not been actually created
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

function cleanup
{
	[[ -e $tmpfile ]] && \
        	log_must $RM -f $tmpfile
}

tmpfile="$TMPDIR/zpool_create_003.tmp${TESTCASE_ID}"

log_assert "'zpool create -n <pool> <vspec> ...' can display the configureation" \
        "without actually creating the pool."

log_onexit cleanup

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

#
# Make sure disk is clean before we use it
#
create_pool $TESTPOOL ${disk}p1 > $tmpfile
destroy_pool $TESTPOOL

$ZPOOL create -n  $TESTPOOL ${disk}p1 > $tmpfile

poolexists $TESTPOOL && \
        log_fail "'zpool create -n <pool> <vspec> ...' fail."

str="would create '$TESTPOOL' with the following layout:"
$CAT $tmpfile | $GREP "$str" >/dev/null 2>&1
(( $? != 0 )) && \
        log_fail "'zpool create -n <pool> <vspec>...' is executed as unexpected."

log_pass "'zpool create -n <pool> <vspec>...' success."
