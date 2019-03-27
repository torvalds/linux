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
# ident	"@(#)zpool_clear_002_neg.ksh	1.3	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_clear_002_neg
#
# DESCRIPTION:
# A badly formed parameter passed to 'zpool clear' should
# return an error.
#
# STRATEGY:
# 1. Create an array containing bad 'zpool clear' parameters.
# 2. For each element, execute the sub-command.
# 3. Verify it returns an error.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && \
		log_must $ZPOOL destroy -f $TESTPOOL1
	[[ -e $file ]] && \
		log_must $RM -f $file
}

log_assert "Execute 'zpool clear' using invalid parameters."
log_onexit cleanup

# Create another pool for negative testing, which clears pool error 
# with vdev device not in the pool vdev devices.
file=$TMPDIR/file.${TESTCASE_ID}
log_must create_vdevs $file
log_must $ZPOOL create $TESTPOOL1 $file

set -A args "" "-?" "--%" "-1234567" "0.0001" "0.7644" "-0.7644" \
		"blah" "blah $DISK" "$TESTPOOL c0txdx" "$TESTPOOL $file" \
		"$TESTPOOL c0txdx blah" "$TESTPOOL $file blah"

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot $ZPOOL clear ${args[i]}

	((i = i + 1))
done

log_pass "Invalid parameters to 'zpool clear' fail as expected."
