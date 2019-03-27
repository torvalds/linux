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
# ident	"@(#)zfs_rename_004_neg.ksh	1.4	07/07/31 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_rename/zfs_rename.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_004_neg
#
# DESCRIPTION:
#       'zfs rename' should fail when this dataset was changed to an existed 
#	dataset name or datasets are of different types.
#       For example, a filesystem cannot be renamed as a volume.
#
# STRATEGY:
#       1. Given a file system, snapshot and volume.
#       2. Rename each dataset object to a different type.
#       3. Verify that only the original name is displayed by zfs list.
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

#
# This array is a list of pairs:
# 	item i: original type
# 	item i + 1: new type
#
set -A bad_dataset $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTCTR1 \
	$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTCTR/$TESTFS1 \
	$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1/$TESTFS1 \
	$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS@snapshot \
	$TESTPOOL/$TESTCTR1 $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTCTR1 $TESTPOOL/$TESTFS@snapshot \
	$TESTPOOL/$TESTCTR1 $TESTPOOL/$TESTFS1 \
	$TESTPOOL/$TESTCTR1 $TESTPOOL/$TESTCTR/$TESTFS1 \
	$TESTPOOL/$TESTCTR/$TESTFS1  $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTCTR/$TESTFS1  $TESTPOOL/$TESTFS@snapshot \
	$TESTPOOL/$TESTCTR/$TESTFS1  $TESTPOOL/$TESTFS1 \
	$TESTPOOL/$TESTCTR/$TESTFS1  $TESTPOOL/$TESTCTR1 \
	$TESTPOOL/$TESTVOL $TESTPOOL/$TESTCTR1 \
	$TESTPOOL/$TESTVOL $TESTPOOL/$TESTFS@snapshot \
	$TESTPOOL/$TESTVOL $TESTPOOL/$TESTFS1 \
	$TESTPOOL/$TESTVOL $TESTPOOL/$TESTCTR/$TESTFS1 \
	$TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTCTR1 \
	$TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTFS1 \
	$TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTCTR/$TESTFS1 \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%c \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%d \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%x \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%p \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%s \
	$TESTPOOL/$TESTFS@snapshot \
	$TESTPOOL/$TESTFS@snapshot/fs

#
# cleanup defined in zfs_rename.kshlib
#
log_onexit cleanup

log_assert "'zfs rename' should fail when datasets are of a different type."

additional_setup

typeset -i i=0
while ((i < ${#bad_dataset[*]} )); do
        log_mustnot $ZFS rename ${bad_dataset[i]} ${bad_dataset[((i + 1))]}
        log_must datasetexists ${bad_dataset[i]}

        log_mustnot $ZFS rename -p ${bad_dataset[i]} ${bad_dataset[((i + 1))]}
        log_must datasetexists ${bad_dataset[i]}

	((i = i + 2))
done

#verify 'rename -p' can not work with snapshots

log_mustnot $ZFS rename -p $TESTPOOL/$TESTFS@snapshot \
		$TESTPOOL/$TESTFS@snapshot2
log_must datasetexists $TESTPOOL/$TESTFS@snapshot
log_mustnot $ZFS rename -p $TESTPOOL/$TESTFS@snapshot \
		$TESTPOOL/$TESTFS/$TESTFS@snapshot2
log_must datasetexists $TESTPOOL/$TESTFS@snapshot

log_pass "'zfs rename' fails as expected when given different dataset types."
