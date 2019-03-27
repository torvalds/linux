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
# ident	"@(#)zfs_rename_010_neg.ksh	1.2	07/07/31 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_010_neg
#
# DESCRIPTION:
#	The recursive flag -r can only be used for snapshots and not for
#	volumes/filesystems.
#
# STRATEGY:
#	1. Loop pool, fs, container and volume.
#	2. Verify none of them can be rename by rename -r.
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

log_assert "The recursive flag -r can only be used for snapshots."

set -A datasets $TESTPOOL		$TESTPOOL/$TESTCTR \
	$TESTPOOL/$TESTCTR/$TESTFS1	$TESTPOOL/$TESTFS
if is_global_zone; then
	datasets[${#datasets[@]}]=$TESTPOOL/$TESTVOL
fi

for opts in "-r" "-r -p"; do
	typeset -i i=0
	while ((i < ${#datasets[@]})); do
		log_mustnot $ZFS rename $opts ${datasets[$i]} \
			${datasets[$i]}-new
	
		# Check datasets, make sure none of them was renamed.
		typeset -i j=0
		while ((j < ${#datasets[@]})); do
			if datasetexists ${datasets[$j]}-new ; then
				log_fail "${datasets[$j]}-new should not exists."
			fi
			((j += 1))
		done
	
		((i += 1))
	done
done

log_pass "The recursive flag -r can only be used for snapshots passed."
