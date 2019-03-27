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
# ident	"@(#)zpool_upgrade_001_pos.ksh	1.3	08/02/27 SMI"
#
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_upgrade_001_pos
#
# DESCRIPTION:
# Executing 'zpool upgrade -v' command succeeds, and also prints a description
# of at least the current ZFS version.
#
# STRATEGY:
# 1. Execute the command
# 2. Verify a 0 exit status
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-07)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Executing 'zpool upgrade -v' command succeeds."

log_must $ZPOOL upgrade -v

# we also check that the usage message contains at least a description 
# of the current ZFS version.

$ZPOOL upgrade -v > $TMPDIR/zpool-versions.${TESTCASE_ID}
COUNT=$( $WC -l $TMPDIR/zpool-versions.${TESTCASE_ID} | $AWK '{print $1}' )
COUNT=$(( $COUNT - 1 ))
$TAIL -${COUNT} $TMPDIR/zpool-versions.${TESTCASE_ID} > $TMPDIR/zpool-versions-desc.${TESTCASE_ID}

#
# Current output for 'zpool upgrade -v' has different indent space
# for single and double digit version number. For example,
#  9   refquota and refreservation properties
#  10  Cache devices
#
log_note "Checking to see we have a description for the current ZFS version."
if (( ZPOOL_VERSION < 10 )); then
	log_must $GREP "$ZPOOL_VERSION   " $TMPDIR/zpool-versions-desc.${TESTCASE_ID}
elif (( ZPOOL_VERSION >= 5000 )); then
	log_must $GREP "The following features are supported" \
		$TMPDIR/zpool-versions-desc.${TESTCASE_ID}
else
	log_must $GREP "$ZPOOL_VERSION  " $TMPDIR/zpool-versions-desc.${TESTCASE_ID}
fi
$RM $TMPDIR/zpool-versions.${TESTCASE_ID}
$RM $TMPDIR/zpool-versions-desc.${TESTCASE_ID}

log_pass "Executing 'zpool upgrade -v' command succeeds."
