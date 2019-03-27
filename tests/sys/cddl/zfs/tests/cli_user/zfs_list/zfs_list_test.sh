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


atf_test_case zfs_list_001_pos cleanup
zfs_list_001_pos_head()
{
	atf_set "descr" "Verify 'zfs list [-rH] [-o property[,prop]*] [fs|clct|vol]'."
	atf_set "require.progs"  zfs
	atf_set "require.user" root
	atf_set "require.config" "unprivileged_user"
}
zfs_list_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_list_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_list_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_list_002_pos cleanup
zfs_list_002_pos_head()
{
	atf_set "descr" "The sort functionality in 'zfs list' works as expected."
	atf_set "require.progs"  zfs
	atf_set "require.user" root
	atf_set "require.config" "unprivileged_user"
}
zfs_list_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_list_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_list_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_list_003_pos cleanup
zfs_list_003_pos_head()
{
	atf_set "descr" "Verify 'zfs list -r' could display any children recursively."
	atf_set "require.progs"  zfs
	atf_set "require.user" root
	atf_set "require.config" "unprivileged_user"
}
zfs_list_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_list_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_list_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_list_004_neg cleanup
zfs_list_004_neg_head()
{
	atf_set "descr" "Verify 'zfs list [-r]' should fail while the givendataset/path does not exist or not belong to zfs."
	atf_set "require.progs"  zfs
	atf_set "require.user" root
	atf_set "require.config" "unprivileged_user"
}
zfs_list_004_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_list_004_neg.ksh || atf_fail "Testcase failed"
}
zfs_list_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_list_005_pos cleanup
zfs_list_005_pos_head()
{
	atf_set "descr" "Verify 'zfs list' evaluate multiple '-s' optionsfrom left to right in decreasing order of importance."
	atf_set "require.progs"  zfs
	atf_set "require.user" root
	atf_set "require.config" "unprivileged_user"
}
zfs_list_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	verify_disk_count "$DISKS" 1
	atf_expect_fail "https://www.illumos.org/issues/8599 Snapshots don't preserve user properties"
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_list_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_list_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_list_006_pos cleanup
zfs_list_006_pos_head()
{
	atf_set "descr" "Verify 'zfs list' exclude list of snapshot."
	atf_set "require.progs"  zfs zpool
	atf_set "require.user" root
	atf_set "require.config" "unprivileged_user"
}
zfs_list_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_list_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_list_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_list_007_pos cleanup
zfs_list_007_pos_head()
{
	atf_set "descr" "'zfs list -d <n>' should get expected output."
	atf_set "require.progs"  zfs
	atf_set "require.user" root
	atf_set "require.config" "unprivileged_user"
}
zfs_list_007_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_list_007_pos.ksh || atf_fail "Testcase failed"
}
zfs_list_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_list_008_neg cleanup
zfs_list_008_neg_head()
{
	atf_set "descr" "A negative depth or a non numeric depth should fail in 'zfs list -d <n>'"
	atf_set "require.progs"  zfs
	atf_set "require.user" root
	atf_set "require.config" "unprivileged_user"
}
zfs_list_008_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_list_008_neg.ksh || atf_fail "Testcase failed"
}
zfs_list_008_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_list.kshlib
	. $(atf_get_srcdir)/zfs_list.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_list_001_pos
	atf_add_test_case zfs_list_002_pos
	atf_add_test_case zfs_list_003_pos
	atf_add_test_case zfs_list_004_neg
	atf_add_test_case zfs_list_005_pos
	atf_add_test_case zfs_list_006_pos
	atf_add_test_case zfs_list_007_pos
	atf_add_test_case zfs_list_008_neg
}
