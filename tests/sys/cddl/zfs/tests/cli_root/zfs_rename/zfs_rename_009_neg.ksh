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
# ident	"@(#)zfs_rename_009_neg.ksh	1.3	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_009_neg
#
# DESCRIPTION:
#	A snapshot already exists with the new name, then none of the
#	snapshots is renamed.
#
# STRATEGY:
#	1. Create snapshot for a set of datasets.
#	2. Create a new snapshot for one of datasets.
#	3. Using rename -r command with exists snapshot name.
#	4. Verify none of the snapshots is renamed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-03-15)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

# Check if current system support recursive rename
$ZFS rename 2>&1 | grep "rename -r" >/dev/null 2>&1
if (($? != 0)); then
	log_unsupported
fi

log_assert "zfs rename -r failed, when snapshot name is already existing."

set -A datasets $TESTPOOL		$TESTPOOL/$TESTCTR \
	$TESTPOOL/$TESTCTR/$TESTFS1	$TESTPOOL/$TESTFS
if is_global_zone; then
	datasets[${#datasets[@]}]=$TESTPOOL/$TESTVOL
fi

log_must $ZFS snapshot -r ${TESTPOOL}@snap
typeset -i i=0
while ((i < ${#datasets[@]})); do
	# Create one more snapshot
	log_must $ZFS snapshot ${datasets[$i]}@snap2
	log_mustnot $ZFS rename -r ${TESTPOOL}@snap ${TESTPOOL}@snap2
	log_must $ZFS destroy ${datasets[$i]}@snap2

	# Check datasets, make sure none of them was renamed.
	typeset -i j=0
	while ((j < ${#datasets[@]})); do
		if datasetexists ${datasets[$j]}@snap2 ; then
			log_fail "${datasets[$j]}@snap2 should not exist."
		fi
		((j += 1))
	done

	((i += 1))
done

log_pass "zfs rename -r failed, when snapshot name is already existing passed."
