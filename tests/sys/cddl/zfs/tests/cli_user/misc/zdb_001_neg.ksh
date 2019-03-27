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
# ident	"@(#)zdb_001_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zdb_001_neg
#
# DESCRIPTION:
#
# zdb can't run as a user on datasets, but can run without arguments
#
# STRATEGY:
# 1. Run zdb as a user, it should print information
# 2. Run zdb as a user on different datasets, it should fail
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

function check_zdb
{
	run_unprivileged $@ > $TMPDIR/zdb.${TESTCASE_ID}
	$GREP "Dataset mos" $TMPDIR/zdb.${TESTCASE_ID}
	if [ $? -eq 0 ]
	then
		log_fail "$@ exited 0 when run as a non root user!"
	fi
	$RM $TMPDIR/zdb.${TESTCASE_ID}
}


function cleanup
{
	if [ -e $TMPDIR/zdb_001_neg.${TESTCASE_ID}.txt ]
	then
		$RM $TMPDIR/zdb_001_neg.${TESTCASE_ID}.txt
	fi

}

verify_runnable "global"

log_assert "zdb can't run as a user on datasets, but can run without arguments"
log_onexit cleanup

run_unprivileged $ZDB > $TMPDIR/zdb_001_neg.${TESTCASE_ID}.txt || log_fail "$ZDB failed"
# verify the output looks okay
log_must $GREP pool_guid $TMPDIR/zdb_001_neg.${TESTCASE_ID}.txt

# we shouldn't able to run it on any dataset
check_zdb $ZDB $TESTPOOL
check_zdb $ZDB $TESTPOOL/$TESTFS
check_zdb $ZDB $TESTPOOL/$TESTFS@snap
check_zdb $ZDB $TESTPOOL/$TESTFS.clone

log_pass "zdb can't run as a user on datasets, but can run without arguments"

