#!/usr/local/bin/ksh93 
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
# ident	"@(#)zfs_copies_006_pos.ksh	1.2	08/02/27 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_copies/zfs_copies.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_copies_006_pos
#
# DESCRIPTION:
# 	Verify that the volume space used by multiple copies is charged correctly
#
# STRATEGY:
#	1. Create volume
#	2. Create UFS filesystem based on the volume
#	3. Set the copies property of volume to 1,2 or 3
#	4. Copy specified size data into each filesystem 
#	5. Verify that the volume space is charged as expected
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-06)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if ismounted $mntp ufs ; then
		log_must $UMOUNT $mntp
	fi
	
	if datasetexists $vol; then
		log_must $ZFS destroy $vol
	fi
	
	if [[ -d $mntp ]]; then
                $RM -rf $mntp
        fi
}


log_assert "Verify that ZFS volume space used by multiple copies is charged correctly."
log_onexit cleanup
mntp=$UFS_MNTPOINT
vol=$TESTPOOL/$TESTVOL1

if [[ ! -d $mntp ]]; then
	$MKDIR -p $mntp
fi 

for val in 1 2 3; do
	do_vol_test ufs $val $mntp
done

log_pass "The volume space used by multiple copies is charged correctly as expected. "
