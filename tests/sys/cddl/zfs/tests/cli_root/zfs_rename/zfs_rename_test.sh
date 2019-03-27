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


atf_test_case zfs_rename_001_pos cleanup
zfs_rename_001_pos_head()
{
	atf_set "descr" "'zfs rename' should successfully rename valid datasets"
	atf_set "require.progs"  zfs
}
zfs_rename_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_rename_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_002_pos cleanup
zfs_rename_002_pos_head()
{
	atf_set "descr" "'zfs rename' should successfully rename valid datasets"
	atf_set "require.progs"  zfs
}
zfs_rename_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_rename_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_003_pos cleanup
zfs_rename_003_pos_head()
{
	atf_set "descr" "'zfs rename' can address the abbreviated snapshot name."
	atf_set "require.progs"  zfs
}
zfs_rename_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_rename_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_004_neg cleanup
zfs_rename_004_neg_head()
{
	atf_set "descr" "'zfs rename' should fail when datasets are of a different type."
	atf_set "require.progs"  zfs
}
zfs_rename_004_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_004_neg.ksh || atf_fail "Testcase failed"
}
zfs_rename_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_005_neg cleanup
zfs_rename_005_neg_head()
{
	atf_set "descr" "'zfs rename' should fail while datasets are within different pool."
	atf_set "require.progs"  zfs
}
zfs_rename_005_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_005_neg.ksh || atf_fail "Testcase failed"
}
zfs_rename_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_006_pos cleanup
zfs_rename_006_pos_head()
{
	atf_set "descr" "'zfs rename' can successfully rename a volume snapshot."
	atf_set "require.progs"  zfs
}
zfs_rename_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_rename_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_007_pos cleanup
zfs_rename_007_pos_head()
{
	atf_set "descr" "Rename dataset, verify that the data haven't changed."
	atf_set "require.progs"  zfs
}
zfs_rename_007_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_007_pos.ksh || atf_fail "Testcase failed"
}
zfs_rename_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_008_pos cleanup
zfs_rename_008_pos_head()
{
	atf_set "descr" "zfs rename -r can rename snapshot recursively."
	atf_set "require.progs"  zfs
}
zfs_rename_008_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_008_pos.ksh || atf_fail "Testcase failed"
}
zfs_rename_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_009_neg cleanup
zfs_rename_009_neg_head()
{
	atf_set "descr" "zfs rename -r failed, when snapshot name is already existing."
	atf_set "require.progs"  zfs
}
zfs_rename_009_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_009_neg.ksh || atf_fail "Testcase failed"
}
zfs_rename_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_010_neg cleanup
zfs_rename_010_neg_head()
{
	atf_set "descr" "The recursive flag -r can only be used for snapshots."
	atf_set "require.progs"  zfs
}
zfs_rename_010_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_010_neg.ksh || atf_fail "Testcase failed"
}
zfs_rename_010_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_011_pos cleanup
zfs_rename_011_pos_head()
{
	atf_set "descr" "'zfs rename -p' should work as expected"
	atf_set "require.progs"  zfs
}
zfs_rename_011_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_011_pos.ksh || atf_fail "Testcase failed"
}
zfs_rename_011_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_012_neg cleanup
zfs_rename_012_neg_head()
{
	atf_set "descr" "'zfs rename' should fail with bad option, null target dataset andtoo long target dataset name."
	atf_set "require.progs"  zfs
}
zfs_rename_012_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_012_neg.ksh || atf_fail "Testcase failed"
}
zfs_rename_012_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_013_pos cleanup
zfs_rename_013_pos_head()
{
	atf_set "descr" "zfs rename -r can rename snapshot when child datasetsdon't have a snapshot of the given name."
	atf_set "require.progs"  zfs
}
zfs_rename_013_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_013_pos.ksh || atf_fail "Testcase failed"
}
zfs_rename_013_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rename.kshlib
	. $(atf_get_srcdir)/zfs_rename.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_rename_001_pos
	atf_add_test_case zfs_rename_002_pos
	atf_add_test_case zfs_rename_003_pos
	atf_add_test_case zfs_rename_004_neg
	atf_add_test_case zfs_rename_005_neg
	atf_add_test_case zfs_rename_006_pos
	atf_add_test_case zfs_rename_007_pos
	atf_add_test_case zfs_rename_008_pos
	atf_add_test_case zfs_rename_009_neg
	atf_add_test_case zfs_rename_010_neg
	atf_add_test_case zfs_rename_011_pos
	atf_add_test_case zfs_rename_012_neg
	atf_add_test_case zfs_rename_013_pos
}
