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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_add_003_pos.ksh	1.4	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_add/zpool_add.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_add_003_pos
#
# DESCRIPTION:
# 	'zpool add -n <pool> <vdev> ...' can display the configuration without
# adding the specified devices to given pool
#
# STRATEGY:
# 	1. Create a storage pool
# 	2. Use -n to add a device to the pool
# 	3. Verify the device is not added actually
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
        poolexists $TESTPOOL && \
                destroy_pool $TESTPOOL

	partition_cleanup

	[[ -e $tmpfile ]] && \
		log_must $RM -f $tmpfile
}

log_assert "'zpool add -n <pool> <vdev> ...' can display the configuration" \
	"without actually adding devices to the pool."

log_onexit cleanup

tmpfile="$TMPDIR/zpool_add_003.tmp${TESTCASE_ID}"

create_pool "$TESTPOOL" "${disk}p1"
log_must poolexists "$TESTPOOL"

$ZPOOL add -n "$TESTPOOL" ${disk}p2 > $tmpfile

log_mustnot iscontained "$TESTPOOL" "${disk}p2"

str="would update '$TESTPOOL' to the following configuration:"
$CAT $tmpfile | $GREP "$str" >/dev/null 2>&1
(( $? != 0 )) && \
	 log_fail "'zpool add -n <pool> <vdev> ...' is executed as unexpected"

log_pass "'zpool add -n <pool> <vdev> ...'executes successfully."
