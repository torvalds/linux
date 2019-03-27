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
# ident	"@(#)zfs_get_003_pos.ksh	1.3	09/01/13 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_get_003_pos
#
# DESCRIPTION:
#	'zfs get' should get consistent report with different options. 
#
# STRATEGY:
#	1. Create pool and filesystem.
#	2. 'zfs mount -o update,noatime <fs>.'
#	3. Verify the value of 'zfs get atime' and 'zfs get all | grep atime'
#	   are identical.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-18)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	log_must $ZFS mount -o update,atime $TESTPOOL/$TESTFS
}

log_assert "'zfs get' should get consistent report with different option."
log_onexit cleanup

log_must $ZFS set atime=on $TESTPOOL/$TESTFS
log_must $ZFS mount -o update,noatime $TESTPOOL/$TESTFS

value1=$($ZFS get -H atime $TESTPOOL/$TESTFS | $AWK '{print $3}')
value2=$($ZFS get -H all $TESTPOOL/$TESTFS | $AWK '{print $2 " " $3}' | \
	$GREP ^atime | $AWK '{print $2}')
if [[ $value1 != $value2 ]]; then
	log_fail "value1($value1) != value2($value2)"
fi

log_pass "'zfs get'  get consistent report with different option passed."
