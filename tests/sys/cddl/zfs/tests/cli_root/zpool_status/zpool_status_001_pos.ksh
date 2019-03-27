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
# ident	"@(#)zpool_status_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_status_001_pos
#
# DESCRIPTION:
# Executing 'zpool status' command with bad options fails.
#
# STRATEGY:
# 1. Create an array of badly formed 'zpool status' options
# 2. Execute each element of the array.
# 3. Verify an error code is returned.
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


set -A args "" "-?" "-x fakepool" "-v fakepool" "-xv fakepool" "-vx fakepool" \
	"-x $TESTPOOL/$TESTFS" "-v $TESTPOOL/$TESTFS" "-xv $TESTPOOL/$TESTFS" \
	"-vx $TESTPOOL/$TESTFS"

log_assert "Executing 'zpool status' with bad options fails"

typeset -i i=1

while [[ $i -lt ${#args[*]} ]]; do

	log_mustnot $ZPOOL status ${args[$i]}

	(( i = i + 1 ))
done

log_pass "'zpool status' command with bad options failed as expected."
