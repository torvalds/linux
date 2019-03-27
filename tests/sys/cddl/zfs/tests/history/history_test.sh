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


atf_test_case history_001_pos
history_001_pos_head()
{
	atf_set "descr" "Verify zpool sub-commands which modify state are logged."
	atf_set "require.progs"  zpool nawk
	atf_set "timeout" 1800
}
history_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/history_001_pos.ksh || atf_fail "Testcase failed"
}

atf_test_case history_002_pos cleanup
history_002_pos_head()
{
	atf_set "descr" "Verify zfs sub-commands which modify state are logged."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
history_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/history_002_pos.ksh || atf_fail "Testcase failed"
}
history_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case history_003_pos cleanup
history_003_pos_head()
{
	atf_set "descr" "zpool history limitation test."
	atf_set "require.progs"  zpool zfs
	atf_set "timeout" 1800
}
history_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/history_003_pos.ksh || atf_fail "Testcase failed"
}
history_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case history_004_pos cleanup
history_004_pos_head()
{
	atf_set "descr" "'zpool history' can copes with many simultaneous command."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
history_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/history_004_pos.ksh || atf_fail "Testcase failed"
}
history_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case history_005_neg cleanup
history_005_neg_head()
{
	atf_set "descr" "Verify 'zpool list|status|iostat' will not be logged."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
history_005_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/history_005_neg.ksh || atf_fail "Testcase failed"
}
history_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case history_006_neg cleanup
history_006_neg_head()
{
	atf_set "descr" "Verify 'zfs list|get|mount|unmount|share|unshare|send' will notbe logged."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
history_006_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/history_006_neg.ksh || atf_fail "Testcase failed"
}
history_006_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case history_007_pos cleanup
history_007_pos_head()
{
	atf_set "descr" "Verify command history moves with pool while migrating."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
history_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/history_007_pos.ksh || atf_fail "Testcase failed"
}
history_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case history_008_pos cleanup
history_008_pos_head()
{
	atf_set "descr" "Internal journal records all the recursively operations."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
history_008_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/history_008_pos.ksh || atf_fail "Testcase failed"
}
history_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case history_009_pos cleanup
history_009_pos_head()
{
	atf_set "descr" "Verify the delegation internal history are correctly."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
history_009_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/history_009_pos.ksh || atf_fail "Testcase failed"
}
history_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case history_010_pos cleanup
history_010_pos_head()
{
	atf_set "descr" "Verify internal long history information are correct."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
history_010_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/history_010_pos.ksh || atf_fail "Testcase failed"
}
history_010_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/history_common.kshlib
	. $(atf_get_srcdir)/history.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case history_001_pos
	atf_add_test_case history_002_pos
	atf_add_test_case history_003_pos
	atf_add_test_case history_004_pos
	atf_add_test_case history_005_neg
	atf_add_test_case history_006_neg
	atf_add_test_case history_007_pos
	atf_add_test_case history_008_pos
	atf_add_test_case history_009_pos
	atf_add_test_case history_010_pos
}
