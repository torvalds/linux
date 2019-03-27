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
# ident	"@(#)zfs_destroy_001_pos.ksh	1.3	09/08/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_destroy/zfs_destroy_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_destroy_001_pos
#
# DESCRIPTION:
#	'zfs destroy -r|-rf|-R|-Rf <fs|ctr|vol|snap>' should recursively destroy
#	all children and clones based on options.
#
# STRATEGY:
#	1. Create test environment according to options. There are three test
#	models can be created. Only ctr, fs & vol; with snap; with clone.
#	2. According to option, make the dataset busy or not.
#	3. Run 'zfs destroy [-rRf] <dataset>'
#	4. According to dataset and option, check if get the expected results.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-22)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

#
# According to parameters, 1st, create suitable testing environment. 2nd, 
# run 'zfs destroy $opt <dataset>'. 3rd, check the system status.
#
# $1 option of 'zfs destroy'
# $2 dataset will be destroied.
#
function test_n_check
{
	typeset opt=$1
	typeset dtst=$2

	if ! is_global_zone ; then
		if [[ $dtst == $VOL || $dtst == $VOLSNAP ]]; then
			log_note "UNSUPPORTED: Volume are unavailable in LZ."
			return
		fi
	fi

	# '-f' has no effect on non-filesystems
	if [[ $opt == -f ]]; then
		if [[ $dtst != $FS || $dtst != $CTR ]]; then
			log_note "UNSUPPORTED: '-f ' is only available for FS."
			return
		fi
	fi

	# Clean the test environment and make it clear.
	if datasetexists $CTR; then
		log_must $ZFS destroy -Rf $CTR
	fi

	# According to option create test compatible environment.
	case $opt in
		-r|-rf) setup_testenv snap ;;
		-R|-Rf) setup_testenv clone ;;
		-f) 	setup_testenv ;;
		*)	log_fail "Incorrect option: '$opt'." ;;
	esac

	#
	# According to different dataset type, create busy condition when try to
	# destroy this dataset.
	#
	typeset mpt_dir
	case $dtst in
		$CTR|$FS)
			if [[ $opt == *f* ]]; then
				mpt_dir=$(get_prop mountpoint $FS)
				make_dir_busy $mpt_dir
				log_mustnot $ZFS destroy -rR $dtst
				make_dir_unbusy $mpt_dir
			fi
			;;
		$VOL)	
			if [[ $opt == *f* ]]; then
				make_dir_busy $TESTDIR1
				log_mustnot $ZFS destroy -rR $dtst
				make_dir_unbusy $TESTDIR1
			fi
			;;
		$FSSNAP)
			if [[ $opt == *f* ]]; then
				mpt_dir=$(snapshot_mountpoint $dtst)
				init_dir=$PWD
				make_dir_busy $mpt_dir
				log_must $ZFS destroy -rR $dtst
				log_must $ZFS snapshot $dtst
				make_dir_unbusy $mpt_dir
			fi
			;;
		$VOLSNAP)
			if [[ $opt == *f* ]]; then
				mpt_dir=$TESTDIR1
				init_dir=$PWD
				make_dir_busy $mpt_dir
				log_must $ZFS destroy -rR $dtst
				log_must $ZFS snapshot $dtst
				make_dir_unbusy $mpt_dir
			fi
			;;
		*)	log_fail "Unsupported dataset: '$dtst'."
	esac

	# Firstly, umount ufs filesystem which was created by zfs volume.
	if is_global_zone; then
		log_must $UMOUNT -f $TESTDIR1
	fi
	# Invoke 'zfs destroy [-rRf] <dataset>'
	log_must $ZFS destroy $opt $dtst

	case $dtst in
		$CTR)	check_dataset datasetnonexists \
					$CTR $FS $VOL $FSSNAP $VOLSNAP
			if [[ $opt == *R* ]]; then
				check_dataset datasetnonexists \
					$FSCLONE $VOLCLONE
			fi
			;;
		$FS)	check_dataset datasetexists $CTR $VOL
			check_dataset datasetnonexists $FS
			if [[ $opt != -f ]]; then
				check_dataset datasetexists $VOLSNAP
				check_dataset datasetnonexists $FSSNAP
			fi
			if [[ $opt == *R* ]]; then
				check_dataset datasetexists $VOLCLONE
				check_dataset datasetnonexists $FSCLONE
			fi
			;;
		$VOL)	check_dataset datasetexists $CTR $FS $FSSNAP
			check_dataset datasetnonexists $VOL $VOLSNAP
			if [[ $opt == *R* ]]; then
				check_dataset datasetexists $FSCLONE
				check_dataset datasetnonexists $VOLCLONE
			fi
			;;
		$FSSNAP)
			check_dataset datasetexists $CTR $FS $VOL $VOLSNAP
			check_dataset datasetnonexists $FSSNAP
			if [[ $opt == *R* ]]; then
				check_dataset datasetexists $VOLCLONE
				check_dataset datasetnonexists $FSCLONE
			fi
			;;
		$VOLSNAP)
			check_dataset datasetexists $CTR $FS $VOL $FSSNAP
			check_dataset datasetnonexists $VOLSNAP
			if [[ $opt == *R* ]]; then
				check_dataset datasetexists $FSCLONE
				check_dataset datasetnonexists $VOLCLONE
			fi
			;;
	esac

	log_note "'$ZFS destroy $opt $dtst' passed."
}

log_assert "'zfs destroy -r|-R|-f|-rf|-Rf <fs|ctr|vol|snap>' should " \
	"recursively destroy all children."
log_onexit cleanup_testenv

typeset dtst=""
typeset opt=""
for dtst in $CTR $FS $VOL $FSSNAP $VOLSNAP; do
	for opt in "-r" "-R" "-f" "-rf" "-Rf"; do
		log_note "Starting test: $ZFS destroy $opt $dtst"
		test_n_check $opt $dtst
	done
done

log_pass "'zfs destroy -r|-R|-f|-rf|-Rf <fs|ctr|vol|snap>' passed."
