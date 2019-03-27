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


atf_test_case zfs_clone_001_neg cleanup
zfs_clone_001_neg_head()
{
	atf_set "descr" "Badly-formed 'zfs clone' with inapplicable scenariosshould return an error."
	atf_set "require.progs"  zfs
}
zfs_clone_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_clone_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_clone_002_pos cleanup
zfs_clone_002_pos_head()
{
	atf_set "descr" "clone -p should work as expected."
	atf_set "require.progs"  zfs
}
zfs_clone_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_clone_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_clone_003_pos cleanup
zfs_clone_003_pos_head()
{
	atf_set "descr" "'zfs clone -o property=value filesystem' can successfully createa ZFS clone filesystem with correct property set."
	atf_set "require.progs"  zfs
}
zfs_clone_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_clone_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_clone_004_pos cleanup
zfs_clone_004_pos_head()
{
	atf_set "descr" "'zfs clone -o property=value filesystem' can successfully createa ZFS clone filesystem with multiple properties set."
	atf_set "require.progs"  zfs
}
zfs_clone_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_clone_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_clone_005_pos cleanup
zfs_clone_005_pos_head()
{
	atf_set "descr" "'zfs clone -o property=value -V size volume' can successfullycreate a ZFS clone volume with correct property set."
	atf_set "require.progs"  zfs
}
zfs_clone_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_clone_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_clone_006_pos cleanup
zfs_clone_006_pos_head()
{
	atf_set "descr" "'zfs clone -o property=value volume' can successfullycreate a ZFS clone volume with multiple correct properties set."
	atf_set "require.progs"  zfs
}
zfs_clone_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_clone_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_clone_007_pos cleanup
zfs_clone_007_pos_head()
{
	atf_set "descr" "'zfs clone -o version=' could upgrade version,but downgrade is denied."
	atf_set "require.progs"  zfs
}
zfs_clone_007_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_007_pos.ksh || atf_fail "Testcase failed"
}
zfs_clone_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_clone_008_neg cleanup
zfs_clone_008_neg_head()
{
	atf_set "descr" "Verify 'zfs clone -o <filesystem>' fails with bad <filesystem> argument."
	atf_set "require.progs"  zfs
}
zfs_clone_008_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_008_neg.ksh || atf_fail "Testcase failed"
}
zfs_clone_008_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_clone_009_neg cleanup
zfs_clone_009_neg_head()
{
	atf_set "descr" "Verify 'zfs clone -o <volume>' fails with bad <volume> argument."
	atf_set "require.progs"  zfs
}
zfs_clone_009_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_009_neg.ksh || atf_fail "Testcase failed"
}
zfs_clone_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_clone.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_clone_001_neg
	atf_add_test_case zfs_clone_002_pos
	atf_add_test_case zfs_clone_003_pos
	atf_add_test_case zfs_clone_004_pos
	atf_add_test_case zfs_clone_005_pos
	atf_add_test_case zfs_clone_006_pos
	atf_add_test_case zfs_clone_007_pos
	atf_add_test_case zfs_clone_008_neg
	atf_add_test_case zfs_clone_009_neg
}
