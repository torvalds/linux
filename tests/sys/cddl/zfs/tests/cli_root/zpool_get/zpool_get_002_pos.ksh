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
# ident	"@(#)zpool_get_002_pos.ksh	1.2	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_get/zpool_get.cfg

################################################################################
#
# __stc_assertion_start
#
# ID:  zpool_get_002_pos
#
# DESCRIPTION:
#
# zpool get all works as expected
#
# STRATEGY:
#
# 1. Using zpool get, retrieve all default values
# 2. Verify that the header is printed
# 3. Verify that we can see all the properties we expect to see
# 4. Verify that the total output contains just those properties + header.
#
# Test for those properties are expected to check whether their
# default values are sane, or whether they can be changed with zpool set.
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

log_assert "Zpool get all works as expected"

typeset -i i=0;

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

log_must $ZPOOL get all $TESTPOOL
$ZPOOL get all $TESTPOOL > $TMPDIR/values.${TESTCASE_ID}

log_note "Checking zpool get all output for a header."
$GREP ^"NAME " $TMPDIR/values.${TESTCASE_ID} > /dev/null 2>&1
if [ $? -ne 0 ]
then
	log_fail "The header was not printed from zpool get all"
fi


while [ $i -lt "${#properties[@]}" ]
do
	log_note "Checking for ${properties[$i]} property"
	$GREP "$TESTPOOL *${properties[$i]}" $TMPDIR/values.${TESTCASE_ID} > /dev/null 2>&1
	if [ $? -ne 0 ]
	then
		log_fail "zpool property ${properties[$i]} was not found\
 in pool output."
	fi
	i=$(( $i + 1 ))
done

# increment the counter to include the header line
i=$(( $i + 1 ))

COUNT=$($WC $TMPDIR/values.${TESTCASE_ID} | $AWK '{print $1}')
if [ $i -ne $COUNT ]
then
	log_fail "Length of output $COUNT was not equal to number of props + 1."
fi



$RM $TMPDIR/values.${TESTCASE_ID}
log_pass "Zpool get all works as expected"
