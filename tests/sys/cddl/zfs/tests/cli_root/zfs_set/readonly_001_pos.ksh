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
# ident	"@(#)readonly_001_pos.ksh	1.1	09/05/19 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: readonly_001_pos
#
# DESCRIPTION:
# Setting readonly on a dataset, it should keep the dataset as readonly.
#
# STRATEGY:
# 1. Create pool, then create filesystem and volume within it.
# 2. Setting readonly to each dataset.
# 3. Check the return value and make sure it is 0.
# 4. Verify the stuff under mountpoint is readonly.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-04-21)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	for dataset in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL ; do
		snapexists ${dataset}@$TESTSNAP && \
			log_must $ZFS destroy -R ${dataset}@$TESTSNAP
	done
}

function initial_dataset # $1 dataset
{
	typeset dataset=$1

	typeset fstype=$(get_prop type $dataset)

	if [[ $fstype == "filesystem" ]] ; then
		typeset mtpt=$(get_prop mountpoint $dataset)
		log_must $TOUCH $mtpt/$TESTFILE0
		log_must $MKDIR -p $mtpt/$TESTDIR0
	fi
}
			

function cleanup_dataset # $1 dataset
{
	typeset dataset=$1

	typeset fstype=$(get_prop type $dataset)

	if [[ $fstype == "filesystem" ]] ; then
		typeset mtpt=$(get_prop mountpoint $dataset)
		log_must $RM -f $mtpt/$TESTFILE0
		log_must $RM -rf $mtpt/$TESTDIR0
	fi
}

function verify_readonly # $1 dataset, $2 on|off
{
	typeset dataset=$1
	typeset value=$2

	if datasetnonexists $dataset ; then
		log_note "$dataset not exist!"
		return 1
	fi

	typeset fstype=$(get_prop type $dataset)

	expect="log_must"

	if [[ $2 == "on" ]] ; then
		expect="log_mustnot"
	fi

	case $fstype in
		filesystem)
			typeset mtpt=$(get_prop mountpoint $dataset)
			$expect $TOUCH $mtpt/$TESTFILE1
			$expect $MKDIR -p $mtpt/$TESTDIR1
			$expect $ECHO 'y' | $RM $mtpt/$TESTFILE0
			$expect $RMDIR $mtpt/$TESTDIR0

			if [[ $expect == "log_must" ]] ; then
				log_must $ECHO 'y' | $RM $mtpt/$TESTFILE1
				log_must $RMDIR $mtpt/$TESTDIR1 
				log_must $TOUCH $mtpt/$TESTFILE0
				log_must $MKDIR -p $mtpt/$TESTDIR0
			fi
			;;
		volume)
			$expect eval "$ECHO 'y' | $NEWFS /dev/zvol/$dataset > /dev/null 2>&1"
			;;
		*)
			;;
	esac
			
	return 0
}

log_onexit cleanup

log_assert "Setting a valid readonly property on a dataset succeeds."

typeset all_datasets

log_must $ZFS mount -a

log_must $ZFS snapshot $TESTPOOL/$TESTFS@$TESTSNAP
log_must $ZFS clone $TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTCLONE

if is_global_zone ; then
	log_must $ZFS snapshot $TESTPOOL/$TESTVOL@$TESTSNAP
	log_must $ZFS clone $TESTPOOL/$TESTVOL@$TESTSNAP $TESTPOOL/$TESTCLONE1
	all_datasets="$TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL/$TESTCLONE $TESTPOOL/$TESTCLONE1"
else
	all_datasets="$TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTCLONE"
fi


for dataset in $all_datasets; do
	for value in on off; do
		set_n_check_prop "off" "readonly" "$dataset"
		initial_dataset $dataset

		set_n_check_prop "$value" "readonly" "$dataset"
		verify_readonly $dataset $value

		set_n_check_prop "off" "readonly" "$dataset"
		cleanup_dataset $dataset
	done
done

log_pass "Setting a valid readonly property on a dataset succeeds."
