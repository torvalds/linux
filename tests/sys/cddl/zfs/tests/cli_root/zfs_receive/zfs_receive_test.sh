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


atf_test_case zfs_receive_001_pos cleanup
zfs_receive_001_pos_head()
{
	atf_set "descr" "Verifying 'zfs receive [<filesystem|snapshot>] -d <filesystem>' works."
	atf_set "require.progs"  zfs
}
zfs_receive_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_receive_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_receive_002_pos cleanup
zfs_receive_002_pos_head()
{
	atf_set "descr" "Verifying 'zfs receive <volume>' works."
	atf_set "require.progs"  zfs
}
zfs_receive_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_receive_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_receive_003_pos cleanup
zfs_receive_003_pos_head()
{
	atf_set "descr" "'zfs recv -F' to force rollback."
	atf_set "require.progs"  zfs
}
zfs_receive_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_receive_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_receive_004_neg cleanup
zfs_receive_004_neg_head()
{
	atf_set "descr" "Verify that invalid parameters to 'zfs receive' are caught."
	atf_set "require.progs"  zfs
}
zfs_receive_004_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_004_neg.ksh || atf_fail "Testcase failed"
}
zfs_receive_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_receive_005_neg cleanup
zfs_receive_005_neg_head()
{
	atf_set "descr" "Verify 'zfs receive' fails with unsupported scenarios."
	atf_set "require.progs"  zfs
}
zfs_receive_005_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_005_neg.ksh || atf_fail "Testcase failed"
}
zfs_receive_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_receive_006_pos cleanup
zfs_receive_006_pos_head()
{
	atf_set "descr" "'zfs recv -d <fs>' should succeed no matter ancestor filesystemexists."
	atf_set "require.progs"  zfs
}
zfs_receive_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_receive_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_receive_007_neg cleanup
zfs_receive_007_neg_head()
{
	atf_set "descr" "'zfs recv -F' should fail if the incremental stream does not match"
	atf_set "require.progs"  zfs
}
zfs_receive_007_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_007_neg.ksh || atf_fail "Testcase failed"
}
zfs_receive_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_receive_008_pos cleanup
zfs_receive_008_pos_head()
{
	atf_set "descr" "Verifying 'zfs receive -vn [<filesystem|snapshot>]and zfs receive -vn -d <filesystem>'"
	atf_set "require.progs"  zfs
}
zfs_receive_008_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_008_pos.ksh || atf_fail "Testcase failed"
}
zfs_receive_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_receive_009_neg cleanup
zfs_receive_009_neg_head()
{
	atf_set "descr" "Verify 'zfs receive' fails with bad option, missing or too many arguments"
	atf_set "require.progs"  zfs
}
zfs_receive_009_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_009_neg.ksh || atf_fail "Testcase failed"
}
zfs_receive_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_receive.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_receive_001_pos
	atf_add_test_case zfs_receive_002_pos
	atf_add_test_case zfs_receive_003_pos
	atf_add_test_case zfs_receive_004_neg
	atf_add_test_case zfs_receive_005_neg
	atf_add_test_case zfs_receive_006_pos
	atf_add_test_case zfs_receive_007_neg
	atf_add_test_case zfs_receive_008_pos
	atf_add_test_case zfs_receive_009_neg
}
