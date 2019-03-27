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
# Copyright 2012 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#


atf_test_case txg_integrity_001_pos cleanup
txg_integrity_001_pos_head()
{
	atf_set "descr" "Ensure that non-aligned writes to the same blocks that cross transaction groups do not corrupt the file."
	atf_set "timeout" 1800
}
txg_integrity_001_pos_body()
{
	export PATH=$(atf_get_srcdir):$PATH
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/txg_integrity.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/txg_integrity_001_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
txg_integrity_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/txg_integrity.cfg


	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case fsync_integrity_001_pos cleanup
fsync_integrity_001_pos_head()
{
	atf_set "descr" "Verify the integrity of non-aligned writes to the same blocks within the same transaction group, where an fsync is issued by a non-final writer."
	atf_set "timeout" 1800
}
fsync_integrity_001_pos_body()
{
	export PATH=$(atf_get_srcdir):$PATH
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/txg_integrity.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/fsync_integrity_001_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
fsync_integrity_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/txg_integrity.cfg
	export DISK="/dev/md${TESTCASE_ID}"
	export TESTDEV=${DISK}p1


	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case txg_integrity_001_pos
	atf_add_test_case fsync_integrity_001_pos
}


save_artifacts()
{
		# If ARTIFACTS_DIR is defined, save test artifacts for
		# post-mortem analysis
		if [[ -n $ARTIFACTS_DIR ]]; then
			TC_ARTIFACTS_DIR=${ARTIFACTS_DIR}/sys/cddl/zfs/tests/txg_integrity/$(atf_get ident)
			mkdir -p $TC_ARTIFACTS_DIR
			cp -a $TESTDIR/$TESTFILE $TC_ARTIFACTS_DIR
			bzip2 $TC_ARTIFACTS_DIR/$TESTFILE
			# Now export the pool and tar up the entire thing
			zpool export $TESTPOOL
			dd if=$TESTDEV bs=131072 of=$TC_ARTIFACTS_DIR/pool
			bzip2 $TC_ARTIFACTS_DIR/pool
			# Reimport it so that the cleanup script will work
			zpool import $TESTPOOL
		fi
}
