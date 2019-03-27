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
# ident	"@(#)canmount_003_pos.ksh	1.2	09/08/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: canmount_003_pos
#
# DESCRIPTION:
# While canmount=noauto and  the dataset is mounted, 
# zfs must not attempt to unmount it.
#
# STRATEGY:
# 1. Setup a pool and create fs, volume, snapshot clone within it.
# 2. Set canmount=noauto for each dataset and check the return value
#    and check if it still can not be unmounted when the dataset is mounted
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-09-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

# check if the testing box support noauto option or not.
$ZFS get 2>&1 | $GREP -w canmount | $GREP -w noauto >/dev/null 2>&1
if (( $? != 0 )); then
	log_unsupported "canmount=noauto is not supported."
fi

set -A dataset_pos "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCLONE"

function cleanup
{
	i=0
	cd $pwd
	while (( i < ${#dataset_pos[*]} )); do
		ds=${dataset_pos[i]}
		if datasetexists $ds; then
			log_must $ZFS set mountpoint=${old_mnt[i]} $ds
			log_must $ZFS set canmount=${old_canmount[i]} $ds
		fi
		(( i = i + 1 ))
	done

	ds=$TESTPOOL/$TESTCLONE
	if datasetexists $ds; then
		mntp=$(get_prop mountpoint $ds)
		log_must $ZFS destroy $ds
		if [[ -d $mntp ]]; then
			log_must $RM -fr $mntp
		fi
	fi
	
	if snapexists $TESTPOOL/$TESTFS@$TESTSNAP ; then
		log_must $ZFS destroy -R $TESTPOOL/$TESTFS@$TESTSNAP
	fi

	unmount_all_safe > /dev/null 2>&1
	log_must $ZFS mount -a
}

log_assert "While canmount=noauto and  the dataset is mounted,"\
		" zfs must not attempt to unmount it"
log_onexit cleanup

set -A old_mnt
set -A old_canmount
typeset ds
typeset pwd=$PWD

log_must $ZFS snapshot $TESTPOOL/$TESTFS@$TESTSNAP
log_must $ZFS clone $TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTCLONE

typeset -i i=0
while (( i < ${#dataset_pos[*]} )); do
	ds=${dataset_pos[i]}
	old_mnt[i]=$(get_prop mountpoint $ds)
	old_canmount[i]=$(get_prop canmount $ds)
	(( i = i + 1 ))
done

i=0 
while (( i < ${#dataset_pos[*]} )) ; do
	dataset=${dataset_pos[i]}
	if  ismounted $dataset; then
		log_must cd ${old_mnt[i]}
		set_n_check_prop "noauto" "canmount" "$dataset"
		log_must mounted $dataset
	fi
	(( i = i + 1 ))
done

log_pass "Setting canmount=noauto to filesystem while dataset busy pass."
