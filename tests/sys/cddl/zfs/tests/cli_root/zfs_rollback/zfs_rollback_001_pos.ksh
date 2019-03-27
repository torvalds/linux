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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfs_rollback_001_pos.ksh	1.4	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_rollback/zfs_rollback_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rollback_001_pos
#
# DESCRIPTION:
# 	'zfs rollback -r|-rf|-R|-Rf' will recursively destroy any snapshots 
#	more recent than the one specified. 
#
# STRATEGY:
#	1. Create pool, fs & volume.
#	2. Separately create three snapshots or clones for fs & volume
#	3. Roll back to the second snapshot and check the results.
#	4. Create the third snapshot or clones for fs & volume again.
#	5. Roll back to the first snapshot and check the results.
#	6. Separately create two snapshots for fs & volume.
#	7. Roll back to the first snapshot and check the results.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "'zfs rollback -r|-rf|-R|-Rf' will recursively destroy any " \
	"snapshots more recent than the one specified."
log_onexit cleanup_env

#
# Create suitable test environment and run 'zfs rollback', then compare with
# expected value to check the system status.
#
# $1 option.
# $2 the number of snapshots or clones.
# $3 the number of snapshot point which we want to rollback.
#
function test_n_check #opt num_snap_clone num_rollback
{
	typeset opt=$1
	typeset -i cnt=$2
	typeset -i pointcnt=$3
	typeset dtst

	(( cnt > 3 || pointcnt > cnt )) && \
		log_fail "Unsupported testing condition."

	# Clean up the test environment
	datasetexists $FS && log_must $ZFS destroy -Rf $FS
	if datasetexists $VOL; then
		$MOUNT | grep -q "/dev/zvol/$VOL" > /dev/null 2>&1
		(( $? == 0 )) && log_must $UMOUNT -f $TESTDIR1

		log_must $ZFS destroy -Rf $VOL
	fi

	# Create specified test environment
	case $opt in
		*r*) setup_snap_env $cnt ;;
		*R*) setup_clone_env $cnt ;;
	esac

	all_snap="$TESTSNAP $TESTSNAP1 $TESTSNAP2"
	all_clone="$TESTCLONE $TESTCLONE1 $TESTCLONE2"
	typeset snap_point
	typeset exist_snap
	typeset exist_clone
	case $pointcnt in
		1) snap_point=$TESTSNAP
		   exist_snap=$TESTSNAP
		   [[ $opt == *R* ]] && exist_clone=$TESTCLONE
		   ;;
		2) snap_point=$TESTSNAP1
		   exist_snap="$TESTSNAP $TESTSNAP1"
		   [[ $opt == *R* ]] && exist_clone="$TESTCLONE $TESTCLONE1"
		   ;;	
	esac

	typeset snap
	for dtst in $FS $VOL; do
		# Volume is not available in Local Zone.
		if [[ $dtst == $VOL ]]; then
			if ! is_global_zone; then
				break
			fi
		fi
		if [[ $opt == *f* ]]; then
			# To write data to the mountpoint directory,
			write_mountpoint_dir $dtst
			opt=${opt%f}
		fi

		if [[ $dtst == $VOL ]]; then
			log_must $UMOUNT -f $TESTDIR1
			log_must $ZFS rollback $opt $dtst@$snap_point
			log_must $MOUNT \
				/dev/zvol/$TESTPOOL/$TESTVOL $TESTDIR1
		else
			log_must $ZFS rollback $opt $dtst@$snap_point
		fi

		for snap in $all_snap; do
			if [[ " $exist_snap " == *" $snap "* ]]; then
				log_must datasetexists $dtst@$snap
			else
				log_must datasetnonexists $dtst@$snap
			fi
		done
		for clone in $all_clone; do
			if [[ " $exist_clone " == *" $clone "* ]]; then
				log_must datasetexists $dtst$clone
			else
				log_must datasetnonexists $dtst$clone
			fi
		done

		check_files $dtst@$snap_point
	done
}

typeset opt
for opt in "-r" "-rf" "-R" "-Rf"; do
	#
	# Currently, the test case was limited to create and rollback 
	# in three snapshots
	#
	log_note "Create 3 snapshots, rollback to the 2nd snapshot " \
		"using $opt."
	test_n_check "$opt" 3 2

	log_note "Create 3 snapshots and rollback to the 1st snapshot " \
		"using $opt."
	test_n_check "$opt" 3 1

	log_note "Create 2 snapshots and rollback to the 1st snapshot " \
		"using $opt."
	test_n_check "$opt" 2 1
done

log_pass "'zfs rollback -r|-rf|-R|-Rf' recursively destroy any snapshots more "\
	"recent than the one specified passed."
