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
# ident	"@(#)zpool_create_020_pos.ksh	1.3	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_create/zpool_create.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_020_pos
#
# DESCRIPTION:
#
# zpool create -R works as expected
#
# STRATEGY:
# 1. Create a -R altroot pool
# 2. Verify the pool is mounted at the correct location
# 3. Verify that cachefile=none for the pool
# 4. Verify that root=<mountpoint> for the pool
# 5. Verify that no reference to the pool is found in /etc/zfs/zpool.cache

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

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	[[ -d ${TESTPOOL}.root ]] && log_must $RM -rf /${TESTPOOL}.root
}

log_onexit cleanup

log_assert "zpool create -R works as expected"

if [[ -n $DISK ]]; then
	disk=$DISK
else
	disk=$DISK0
fi

$RM -rf /${TESTPOOL}.root
log_must $MKDIR /${TESTPOOL}.root
log_must $ZPOOL create -R /${TESTPOOL}.root $TESTPOOL $disk
if [ ! -d /${TESTPOOL}.root ]
then
	log_fail "Mountpoint was not create when using zpool with -R flag!"
fi

FS=$($ZFS list $TESTPOOL)
if [ -z "$FS" ]
then
	log_fail "Mounted filesystem at /${TESTPOOL}.root isn't ZFS!"
fi

log_must $ZPOOL get all $TESTPOOL
$ZPOOL get all $TESTPOOL > $TMPDIR/values.${TESTCASE_ID}

# check for the cachefile property, verifying that it's set to 'none'
$GREP "$TESTPOOL[ ]*cachefile[ ]*none" $TMPDIR/values.${TESTCASE_ID} > /dev/null 2>&1
if [ $? -ne 0 ]
then
	log_fail "zpool property \'cachefile\' was not set to \'none\'."
fi

# check that the root = /mountpoint property is set correctly
$GREP "$TESTPOOL[ ]*altroot[ ]*/${TESTPOOL}.root" $TMPDIR/values.${TESTCASE_ID} > /dev/null 2>&1
if [ $? -ne 0 ]
then
	log_fail "zpool property root was not found in pool output."
fi

$RM $TMPDIR/values.${TESTCASE_ID}

# finally, check that the pool has no reference in /etc/zfs/zpool.cache
if [[ -f /etc/zfs/zpool.cache ]] ; then
	REF=$($STRINGS /etc/zfs/zpool.cache | $GREP ${TESTPOOL})
	if [ ! -z "$REF" ]
	then
		$STRINGS /etc/zfs/zpool.cache	
		log_fail "/etc/zfs/zpool.cache appears to have a reference to $TESTPOOL"
	fi
fi


log_pass "zpool create -R works as expected"
