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
# ident	"@(#)zfs_set_001_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_set_001_neg
#
# DESCRIPTION:
#
# zfs set returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to set an array of properties on a dataset
# 2. Verify that those properties were not set and retain their original values.
#
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

log_assert "zfs set returns an error when run as a user"

typeset -i i=0

set -A props $PROP_NAMES
set -A prop_vals $PROP_VALS
set -A prop_new $PROP_ALTVALS

while [[ $i -lt ${#args[*]} ]]
do	
	PROP=${props[$i]}
	EXPECTED=${prop_vals[$i]}
	NEW=${prop_new[$i]}
	log_mustnot run_unprivileged "$ZFS set $PROP=$NEW $TESTPOOL/$TESTFS/prop"
	
	# Now verify that the above command did nothing
	ACTUAL=$($ZFS get $PROP -o value -H snapdir $TESTPOOl/$TESTFS/prop )
	if [ "$ACTUAL" != "$EXPECTED" ]
	then
		log_fail "Property $PROP was set to $ACTUAL, expected $EXPECTED"
	fi
        i=$(( $i + 1 ))
done

log_pass "zfs set returns an error when run as a user"
