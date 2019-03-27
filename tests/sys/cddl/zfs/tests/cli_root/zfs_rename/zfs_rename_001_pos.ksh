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
# ident	"@(#)zfs_rename_001_pos.ksh	1.4	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_rename/zfs_rename.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_001_pos
#
# DESCRIPTION:
#       'zfs rename' should successfully rename valid datasets.
#       As a sub-assertion we check to ensure the datasets that can
#       be mounted are mounted.
#
# STRATEGY:
#       1. Given a file system, snapshot and volume.
#       2. Rename each dataset object to a new name.
#       3. Verify that only the new name is displayed by zfs list.
#       4. Verify mountable datasets are mounted.
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
set -A mountable "$TESTPOOL/$TESTFS1-new" "$TESTPOOL/$TESTFS@snapshot-new" \
    "$TESTPOOL/$TESTCTR/$TESTFS1-new" "$TESTPOOL/$TESTFS-clone-new"

#
# cleanup defined in zfs_rename.kshlib
#
log_onexit cleanup

log_assert "'zfs rename' should successfully rename valid datasets"

additional_setup

typeset -i i=0
while (( i < ${#dataset[*]} )); do
	rename_dataset ${dataset[i]} ${dataset[i]}-new

	((i = i + 1))
done

log_note "Verify mountable datasets are mounted in their new namespace."
typeset mtpt
i=0
while (( i < ${#mountable[*]} )); do
	# Snapshot have no mountpoint
	if [[ ${mountable[i]} != *@* ]]; then
		log_must mounted ${mountable[i]}
		mtpt=$(get_prop mountpoint ${mountable[i]})
	else
		mtpt=$(snapshot_mountpoint ${mountable[i]})
	fi

	if ! cmp_data $DATA $mtpt/$TESTFILE0 ; then
		log_fail "$mtpt/$TESTFILE0 gets corrupted after rename operation."
	fi

	((i = i + 1))
done

#verify the data integrity in zvol
if is_global_zone; then
	log_must eval "$DD if=${VOL_R_PATH}-new of=$VOLDATA bs=$BS count=$CNT >/dev/null 2>&1"
	if ! cmp_data $VOLDATA $DATA ; then
		log_fail "$VOLDATA gets corrupted after rename operation."
	fi
fi

# rename back fs
typeset -i i=0
while ((i < ${#dataset[*]} )); do
	if datasetexists ${dataset[i]}-new ; then
                log_must $ZFS rename ${dataset[i]}-new ${dataset[i]}
	fi
        ((i = i + 1))
done

log_pass "'zfs rename' successfully renamed each dataset type."
