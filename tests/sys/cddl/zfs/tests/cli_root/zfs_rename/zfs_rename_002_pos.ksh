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
# ident	"@(#)zfs_rename_002_pos.ksh	1.3	07/07/31 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_rename/zfs_rename.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_002_pos
#
# DESCRIPTION: 
#       'zfs rename' should successfully be capable of renaming
#       valid datasets back and forth multiple times.
#
# STRATEGY:
#       1. Given a file system, snapshot and volume.
#       2. Rename each dataset object to a new name.
#       3. Rename each dataset back to its original name.
#       4. Repeat steps 2 and 3 multiple times.
#       5. Verify that the correct name is displayed by zfs list.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-06-29)
#
# __stc_assertion_end
#
############################################################################### 

verify_runnable "both"

set -A dataset "$TESTPOOL/$TESTFS@snapshot" "$TESTPOOL/$TESTFS1" \
   "$TESTPOOL/$TESTCTR/$TESTFS1" "$TESTPOOL/$TESTCTR1" \
    "$TESTPOOL/$TESTVOL" "$TESTPOOL/$TESTFS-clone"

#
# cleanup defined in zfs_rename.kshlib
#
log_onexit cleanup

log_assert "'zfs rename' should successfully rename valid datasets"

additional_setup

typeset -i i=0
typeset -i iters=10

while ((i < ${#dataset[*]} )); do
	j=0
	while ((j < iters )); do
		rename_dataset ${dataset[i]} ${dataset[i]}-new
		rename_dataset ${dataset[i]}-new ${dataset[i]}

		((j = j + 1))
	done

	if [[ ${dataset[i]} == *@* ]]; then
		data=$(snapshot_mountpoint ${dataset[i]})/$TESTFILE0
	elif [[ ${dataset[i]} == "$TESTPOOL/$TESTVOL" ]] && is_global_zone; then
		log_must eval "$DD if=$VOL_R_PATH of=$VOLDATA bs=$BS count=$CNT >/dev/null 2>&1"
		data=$VOLDATA
	else
		data=$(get_prop mountpoint ${dataset[i]})/$TESTFILE0
	fi	

	if ! cmp_data $DATA $data; then
		log_fail "$data gets corrupted after $iters times rename operations."
	fi

	((i = i + 1))
done

log_pass "'zfs rename' renamed each dataset type multiple times as expected."
