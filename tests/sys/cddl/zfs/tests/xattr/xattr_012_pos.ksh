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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)xattr_012_pos.ksh	1.2	08/02/27 SMI"
#

# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/xattr/xattr_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  xattr_012_pos
#
# DESCRIPTION:
# xattr file sizes count towards normal disk usage
# 
# STRATEGY:
#	1. Create a file, and check pool and filesystem usage
#       2. Create a 200mb xattr in that file
#	3. Check pool and filesystem usage, to ensure it reflects the size
#	   of the xattr
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-15)
#
# __stc_assertion_end
#
################################################################################

function cleanup {
	log_must $RM $TESTDIR/myfile.${TESTCASE_ID}
}

function get_pool_size	{
	poolname=$1
	psize=$( $ZPOOL list -H -o used $poolname )
	if [[ $psize == *[mM] ]]
	then
		returnvalue=$($ECHO $psize | $SED -e 's/m//g' -e 's/M//g')	
		returnvalue=$(( returnvalue * 1024 ))
	else
		returnvalue=$($ECHO $psize | $SED -e 's/k//g' -e 's/K//g')	
	fi
	print $returnvalue
}

log_assert "xattr file sizes count towards normal disk usage"
log_onexit cleanup

test_requires RUNAT

log_must $TOUCH $TESTDIR/myfile.${TESTCASE_ID}

POOL_SIZE=0
NEW_POOL_SIZE=0

if is_global_zone
then
	# get pool and filesystem sizes. Since we're starting with an empty
	# pool, the usage should be small - a few k.
	POOL_SIZE=$(get_pool_size $TESTPOOL) 
fi

FS_SIZE=$( $ZFS get -p -H -o value used $TESTPOOL/$TESTFS )

log_must $RUNAT $TESTDIR/myfile.${TESTCASE_ID} $MKFILE 200m xattr

#Make sure the newly created file is counted into zpool usage 
log_must $SYNC

# now check to see if our pool disk usage has increased
if is_global_zone
then
	NEW_POOL_SIZE=$(get_pool_size $TESTPOOL) 
	if (( $NEW_POOL_SIZE <= $POOL_SIZE ))
	then
		log_fail "The new pool size $NEW_POOL_SIZE was less \
                than or equal to the old pool size $POOL_SIZE."
	fi

fi

# also make sure our filesystem usage has increased
NEW_FS_SIZE=$( $ZFS get -p -H -o value used $TESTPOOL/$TESTFS )
if (( $NEW_FS_SIZE <= $FS_SIZE ))
then
	log_fail "The new filesystem size $NEW_FS_SIZE was less \
		than or equal to the old filesystem size $FS_SIZE."
fi

log_pass "xattr file sizes count towards normal disk usage"
