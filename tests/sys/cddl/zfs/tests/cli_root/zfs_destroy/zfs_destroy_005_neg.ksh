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
# ident	"@(#)zfs_destroy_005_neg.ksh	1.3	09/08/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_destroy/zfs_destroy_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_destroy_005_neg
#
# DESCRIPTION:
#	Separately verify 'zfs destroy -f|-r|-rf|-R|-rR <dataset>' will fail in 
#       different conditions.
#
# STRATEGY:
#	1. Create pool, fs & vol.
#	2. Create snapshot for fs & vol.
#	3. Invoke 'zfs destroy ''|-f <dataset>', it should fail.
#	4. Create clone for fs & vol.
#	5. Invoke 'zfs destroy -r|-rf <dataset>', it should fail.
#	6. Write file to filesystem or enter snapshot mountpoint.
#	7. Invoke 'zfs destroy -R|-rR <dataset>', it should fail.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-08-03)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Separately verify 'zfs destroy -f|-r|-rf|-R|-rR <dataset>' will " \
	"fail in different conditions."
log_onexit cleanup_testenv

#
# Run 'zfs destroy [-rRf] <dataset>', make sure it fail.
#
# $1 the collection of options
# $2 the collection of datasets
#
function negative_test
{
	typeset options=$1
	typeset datasets=$2

	for dtst in $datasets; do
		if ! is_global_zone; then
			if [[ $dtst == $VOL || $dtst == $VOLSNAP || \
				$dtst == $VOLCLONE ]]
			then
				log_note "UNSUPPORTED: " \
					"Volume is unavailable in LZ."
				continue
			fi
		fi
		for opt in $options; do
			log_mustnot $ZFS destroy $opt $dtst
		done
	done
}

# This filesystem is created by setup.ksh, and conflicts with the filesystems
# created from within this file
$ZFS destroy -f $TESTPOOL/$TESTFS

#
# Create snapshots for filesystem and volume, 
# and verify 'zfs destroy' failed without '-r' or '-R'.
#
setup_testenv snap
negative_test "-f" "$CTR $FS $VOL"

#
# Create clones for filesystem and volume,
# and verify 'zfs destroy' failed without '-R'.
#
setup_testenv clone
negative_test "-r -rf" "$CTR $FS $VOL"

#
# Get $FS mountpoint and make it busy, then verify 'zfs destroy $CTR' 
# failed without '-f'.
#
# Then verify the datasets are expected existed or non-existed.
#
typeset mtpt_dir=$(get_prop mountpoint $FS)
make_dir_busy $mtpt_dir
negative_test "-R -rR" $CTR

#
# Checking the outcome of the test above is tricky, because the order in
# which datasets are destroyed is not deterministic. Both $FS and $VOL are
# busy, and the remaining datasets will be different depending on whether we
# tried (and failed) to delete $FS or $VOL first.

# The following datasets will exist independent of the order
check_dataset datasetexists $CTR $FS $VOL

if datasetexists $VOLSNAP && datasetnonexists $FSSNAP; then
	# The recursive destroy failed on $FS
	check_dataset datasetnonexists $FSSNAP $FSCLONE
	check_dataset datasetexists $VOLSNAP $VOLCLONE
elif datasetexists $FSSNAP && datasetnonexists $VOLSNAP; then
	# The recursive destroy failed on $VOL
	check_dataset datasetnonexists $VOLSNAP $VOLCLONE
	check_dataset datasetexists $FSSNAP $FSCLONE
else
	log_must zfs list -rtall
	log_fail "Unexpected datasets remaining"
fi

#
# Create the clones for test environment, then verify 'zfs destroy $FS'
# failed without '-f'. 
#
# Then verify the datasets are expected existed or non-existed.
#
setup_testenv clone	
negative_test "-R -rR" $FS
check_dataset datasetexists $CTR $FS $VOL $VOLSNAP $VOLCLONE
log_must datasetnonexists $FSSNAP $FSCLONE 

make_dir_unbusy $mtpt_dir

if is_global_zone; then
	#
	# Create the clones for test environment and make the volume busy.
	# Then verify 'zfs destroy $CTR' failed without '-f'. 
	#
	# Then verify the datasets are expected existed or non-existed.
	#
	setup_testenv clone
	make_dir_busy $TESTDIR1
	negative_test "-R -rR" $CTR
	log_must datasetexists $CTR $VOL
	log_must datasetnonexists $VOLSNAP $VOLCLONE

	# Here again, the non-determinism of destroy order is a factor. $FS,
	# $FSSNAP and $FSCLONE will still exist here iff we attempted to destroy
	# $VOL (and failed) first. So check that either all of the datasets are
	# present, or they're all gone.
	if datasetexists $FS; then
		check_dataset datasetexists $FS $FSSNAP $FSCLONE
	else
		check_dataset datasetnonexists $FS $FSSNAP $FSCLONE
	fi

	#
	# Create the clones for test environment and make the volume busy.
	# Then verify 'zfs destroy $VOL' failed without '-f'. 
	#
	# Then verify the datasets are expected existed or non-existed.
	#
	setup_testenv clone
	negative_test "-R -rR" $VOL
	log_must datasetexists $CTR $VOL $FS $FSSNAP $FSCLONE
	log_must datasetnonexists $VOLSNAP $VOLCLONE

	make_dir_unbusy $TESTDIR1
fi

#
# Create the clones for test environment and make the snapshot busy.
# Then verify 'zfs destroy $snap' failed without '-f'. 
#
# Then verify the datasets are expected existed or non-existed.
#
snaplist="$FSSNAP"

setup_testenv clone
for snap in $snaplist; do
	for option in -R -rR ; do
		mtpt_dir=$(snapshot_mountpoint $snap)
		(( $? != 0 )) && \
			log_fail "get mountpoint $snap failed."
	
		init_dir=$PWD
		log_must cd $mtpt_dir

		log_must $ZFS destroy $option $snap
		check_dataset datasetexists $CTR $FS $VOL
		if [[ $snap == $FSSNAP ]]; then
			log_must datasetnonexists $snap $FSCLONE
		else
			log_must datasetnonexists $snap $VOLCLONE
		fi
		setup_testenv clone
	done
done

cmds="zfs destroy -f|-r|-rf|-R|-rR <dataset>"
log_pass "'$cmds' must fail in certain conditions."
