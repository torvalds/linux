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
# ident	"@(#)mmap_write_001_pos.ksh	1.3	07/07/31 SMI"
#
. $STF_SUITE/include/libtest.kshlib

# ##########################################################################
#
# __stc_assertion_start
#
# ID: mmap_write_001_pos
#
# DESCRIPTION:
# Writing to a file and mmaping that file at the
# same time does not result in a deadlock.
#
# STRATEGY:
# 1. Make sure this test executes on multi-processes system.
# 2. Call mmapwrite binary.
# 3. wait 120s and make sure the test file existed.
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

# Default is 120 seconds or 2 minutes
WAITTIME=${WAITTIME-120}

log_assert "write()s to a file and mmap() that file at the same time does not "\
	"result in a deadlock."

# Detect and make sure this test must be executed on a multi-process system
NCPUS=`sysctl -a | awk -F '"' '/cpu count="[0-9+]"/ {print $2; exit}'`
if [[ $? -ne 0 || -z $NCPUS || $NCPUS -le 1 ]]; then
	log_unsupported "This test must be executed on a multi-processor system."
fi

log_must $CHMOD 777 $TESTDIR
$MMAPWRITE $TESTDIR/$TESTFILE &
PID_MMAPWRITE=$!
log_note "$MMAPWRITE $TESTDIR/$TESTFILE pid: $PID_MMAPWRITE"
log_must $SLEEP 10

typeset -i i=0
while (( i < $WAITTIME )); do
	if ! $PS -ef | $PGREP $MMAPWRITE > /dev/null ; then
		log_must $WAIT $PID_MMAPWRITE
		break
	fi
	$SLEEP 1
	(( i += 1 ))
done

if $PS -ef | $PGREP $MMAPWRITE > /dev/null ; then
	log_must $KILL -9 $PID_MMAPWRITE
fi
log_must $LS -l $TESTDIR/$TESTFILE

log_pass "write(2) a mmap(2)'ing file succeeded."
