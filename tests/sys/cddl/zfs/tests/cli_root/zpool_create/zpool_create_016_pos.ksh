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
# ident	"@(#)zpool_create_016_pos.ksh	1.2	08/08/15 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_016_pos
#
#
# DESCRIPTION:
# 'zpool create' will success with no device in swap
#
#
# STRATEGY:
# 1. delete all devices in the swap
# 2. create a zpool
# 3. Verify the creation is successed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-04-17)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL

	#recover swap devices
	FSTAB=$TMPDIR/fstab_${TESTCASE_ID}
	$RM -f $FSTAB
	for sdisk in $swap_disks; do
		$ECHO "$sdisk	-	-	swap	-	no	-" >> $FSTAB
	done
	if [ -e $FSTAB ]
	then
		log_must $SWAPADD $FSTAB
	fi
	$RM -f $FSTAB
	if [ $dump_device != "none" ]
	then
		log_must $DUMPADM -u -d $dump_device
	fi
}

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi
typeset pool_dev=${disk}p1
typeset swap_disks=`$SWAP -l | $GREP -v "swapfile" | $AWK '{print $1}'`
typeset dump_device=`$DUMPADM | $GREP "Dump device" | $AWK '{print $3}'`
	    
log_assert "'zpool create' should success with no device in swap."
log_onexit cleanup

for sdisk in $swap_disks; do
	log_must $SWAP -d $sdisk
done

log_must $ZPOOL create $TESTPOOL $pool_dev

log_pass "'zpool create' passed as expected with applicable scenario."
