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
#ident	"@(#)snapdir_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: snapdir_001_pos
#
# DESCRIPTION:
# Setting a valid snapdir on a dataset, it should be successful.
#
# STRATEGY:
# 1. Create pool, then create filesystem and volume within it.
# 2. Create a snapshot for each dataset.
# 3. Setting different valid snapdir to each dataset.
# 4. Check the return value and make sure it is 0.
# 5. Verify .zfs directory is hidden|visible according to the snapdir setting.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-02-17)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	for dataset in $all_datasets; do
		snapexists ${dataset}@snap && \
			log_must $ZFS destroy ${dataset}@snap
	done
}

function verify_snapdir_visible # $1 dataset, $2 hidden|visible
{
	typeset dataset=$1
	typeset value=$2
	typeset mtpt=$(get_prop mountpoint $dataset)
	typeset name

	CTLDIR=".zfs"

	for name in `$LS -a $mtpt`; do
		if [[ $name == $CTLDIR ]]; then
			if [[ $value == "visible" ]]; then
				return 0
			else
				return 1
			fi
		fi
	done

	if [[ $value == "visible" ]]; then
		return 1
	else
		return 0
	fi		
}


typeset all_datasets

if is_global_zone ; then
	all_datasets="$TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL"
else
	all_datasets="$TESTPOOL $TESTPOOL/$TESTFS"
fi

log_onexit cleanup

for dataset in $all_datasets; do
	log_must $ZFS snapshot ${dataset}@snap
done

log_assert "Setting a valid snapdir property on a dataset succeeds."

for dataset in $all_datasets; do
	for value in hidden visible; do
		if [[ $dataset == "$TESTPOOL/$TESTVOL" ]] ; then
			set_n_check_prop "$value" "snapdir" \
				"$dataset" "false"
		else
			set_n_check_prop "$value" "snapdir" \
				"$dataset"
			verify_snapdir_visible $dataset $value
			[[ $? -eq 0 ]] || \
				log_fail "$dataset/.zfs is not $value as expect."
		fi
	done
done

log_pass "Setting a valid snapdir property on a dataset succeeds."
