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
# ident	"@(#)zpool_get_003_pos.ksh	1.2	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_get/zpool_get.cfg

################################################################################
#
# __stc_assertion_start
#
# ID:  zpool_get_003_pos
#
# DESCRIPTION:
#
# Zpool get returns values for all known properties
#
# STRATEGY:
# 1. For all properties, verify zpool get retrieves a value
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

log_assert "Zpool get returns values for all known properties"

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

typeset -i i=0;

while [ $i -lt "${#properties[@]}" ]
do
	log_note "Checking for ${properties[$i]} property"
	log_must eval "$ZPOOL get ${properties[$i]} $TESTPOOL > $TMPDIR/value.${TESTCASE_ID}"
	$GREP "${properties[$i]}" $TMPDIR/value.${TESTCASE_ID} > /dev/null 2>&1
	if [ $? -ne 0 ]
	then
		log_fail "${properties[$i]} not seen in output"
	fi
	$GREP "^NAME " $TMPDIR/value.${TESTCASE_ID} > /dev/null 2>&1
	# only need to check this once.
	if [ $i -eq 0 ] && [ $? -ne 0 ]
	then
		log_fail "Header not seen in zpool get output"
	fi
	i=$(( $i + 1 ))
done

$RM $TMPDIR/value.${TESTCASE_ID}
log_pass "Zpool get returns values for all known properties"
