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
# ident	"@(#)zfs_create_013_pos.ksh	1.1	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/zfs_create.cfg

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_create_013_pos
#
# DESCRIPTION:
# 'zfs create -s -V <size> <volume>' can create various-size sparse volume
#  with long fs name
#
# STRATEGY:
# 1. Create a volume in the storage pool.
# 2. Verify the volume is created correctly.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-08-07)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	typeset -i j=0
        while [[ $j -lt ${#size[*]} ]]; do
               	datasetexists $TESTPOOL/${LONGFSNAME}${size[j]} && \
               		log_must $ZFS destroy $TESTPOOL/${LONGFSNAME}${size[j]}
                ((j = j + 1))
        done
}

log_onexit cleanup


log_assert "'zfs create -s -V <size> <volume>' succeeds" 

typeset -i j=0
while (( $j < ${#size[*]} )); do
	typeset cmdline="$ZFS create -s -V ${size[j]} \
			 $TESTPOOL/${LONGFSNAME}${size[j]}"

	str=$(eval $cmdline 2>&1)
	if (( $? == 0 )); then
		log_note "SUCCESS: $cmdline"
		log_must datasetexists $TESTPOOL/${LONGFSNAME}${size[j]}
	elif [[ $str == *${VOL_LIMIT_KEYWORD1}* || \
		$str == *${VOL_LIMIT_KEYWORD2}* || \
		$str == *${VOL_LIMIT_KEYWORD3}* ]]
	then
		log_note "UNSUPPORTED: $cmdline"
	else
		log_fail "$cmdline"
	fi

	((j = j + 1))
done

log_pass "'zfs create -s -V <size> <volume>' works as expected."
