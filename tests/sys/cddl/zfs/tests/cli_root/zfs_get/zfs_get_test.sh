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


atf_test_case zfs_get_001_pos cleanup
zfs_get_001_pos_head()
{
	atf_set "descr" "Setting the valid options and properties 'zfs get' should returnthe correct property value."
	atf_set "require.progs"  zfs
}
zfs_get_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_get_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_002_pos cleanup
zfs_get_002_pos_head()
{
	atf_set "descr" "Setting the valid options and properties 'zfs get' return correctvalue. It should be successful."
	atf_set "require.progs"  zfs
}
zfs_get_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_get_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_003_pos cleanup
zfs_get_003_pos_head()
{
	atf_set "descr" "'zfs get' should get consistent report with different option."
	atf_set "require.progs"  zfs
}
zfs_get_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_get_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_004_pos cleanup
zfs_get_004_pos_head()
{
	atf_set "descr" "Verify the functions of 'zfs get all' work."
	atf_set "require.progs"  zfs zpool
}
zfs_get_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_get_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_005_neg cleanup
zfs_get_005_neg_head()
{
	atf_set "descr" "Setting the invalid option and properties, 'zfs get' should befailed."
	atf_set "require.progs"  zfs
}
zfs_get_005_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_005_neg.ksh || atf_fail "Testcase failed"
}
zfs_get_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_006_neg cleanup
zfs_get_006_neg_head()
{
	atf_set "descr" "Verify 'zfs get all' fails with invalid combination scenarios."
	atf_set "require.progs"  zfs
}
zfs_get_006_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_006_neg.ksh || atf_fail "Testcase failed"
}
zfs_get_006_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_007_neg cleanup
zfs_get_007_neg_head()
{
	atf_set "descr" "'zfs get -o' fails with invalid options or column names"
	atf_set "require.progs"  zfs
}
zfs_get_007_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_007_neg.ksh || atf_fail "Testcase failed"
}
zfs_get_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_008_pos cleanup
zfs_get_008_pos_head()
{
	atf_set "descr" "Verify '-d <n>' can work with other options"
	atf_set "require.progs"  zfs
}
zfs_get_008_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_008_pos.ksh || atf_fail "Testcase failed"
}
zfs_get_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_009_pos cleanup
zfs_get_009_pos_head()
{
	atf_set "descr" "'zfs get -d <n>' should get expected output."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
zfs_get_009_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_009_pos.ksh || atf_fail "Testcase failed"
}
zfs_get_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_010_neg cleanup
zfs_get_010_neg_head()
{
	atf_set "descr" "A negative depth or a non numeric depth should fail in 'zfs get -d <n>'"
	atf_set "require.progs"  zfs
}
zfs_get_010_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_010_neg.ksh || atf_fail "Testcase failed"
}
zfs_get_010_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_get_list_d.kshlib
	. $(atf_get_srcdir)/zfs_get_common.kshlib
	. $(atf_get_srcdir)/zfs_get.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_get_001_pos
	atf_add_test_case zfs_get_002_pos
	atf_add_test_case zfs_get_003_pos
	atf_add_test_case zfs_get_004_pos
	atf_add_test_case zfs_get_005_neg
	atf_add_test_case zfs_get_006_neg
	atf_add_test_case zfs_get_007_neg
	atf_add_test_case zfs_get_008_pos
	atf_add_test_case zfs_get_009_pos
	atf_add_test_case zfs_get_010_neg
}
