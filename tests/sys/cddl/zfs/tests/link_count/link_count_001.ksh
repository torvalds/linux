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
# ident	"@(#)link_count_001.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: link_count_001
#
# DESCRIPTION:
# Verify file link count is zero on zfs
#
# STRATEGY:
# 1. Make sure this test executes on multi-processes system
# 2. Make zero size files and remove them in the background
# 3. Call the binary
# 4. Make sure the files can be removed successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify file link count is zero on zfs"

# Detect and make sure this test must be executed on a multi-process system
NCPUS=`sysctl -n kern.smp.cpus`
#NCPUS=`sysctl -a | awk -F '"' '/cpu count="[0-9+]"/ {print $2; exit}'`
if [[ $? -ne 0 || -z "$NCPUS" || "$NCPUS" -le 1 ]]; then
	log_unsupported "This test must be executed on a multi-processor system."
fi

log_must $MKDIR -p ${TESTDIR}/tmp

typeset -i i=0
while [ $i -lt $NUMFILES ]; do
        (( i = i + 1 ))
        $TOUCH ${TESTDIR}/tmp/x$i > /dev/null 2>&1
done

sleep 3

$RM -f ${TESTDIR}/tmp/x* >/dev/null 2>&1

$RM_LNKCNT_ZERO_FILE ${TESTDIR}/tmp/test${TESTCASE_ID} > /dev/null 2>&1 &
PID=$!
log_note "$RM_LNKCNT_ZERO_FILE ${TESTDIR}/tmp/test${TESTCASE_ID} pid: $PID"

i=0
while [ $i -lt $ITERS ]; do
	if ! $PGREP $RM_LNKCNT_ZERO_FILE > /dev/null ; then
		log_note "$RM_LNKCNT_ZERO_FILE completes"
		break
	fi
	log_must $SLEEP 10
	(( i = i + 1 ))
done	

if $PGREP $RM_LNKCNT_ZERO_FILE > /dev/null; then
	log_must $KILL -9 $PID
	log_fail "file link count is zero"
fi

log_must $RM -f ${TESTDIR}/tmp/test${TESTCASE_ID}*

log_pass "Verify file link count is zero on zfs"
