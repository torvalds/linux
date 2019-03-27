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


atf_test_case clone_001_pos cleanup
clone_001_pos_head()
{
	atf_set "descr" "Verify a cloned file system is writable."
	atf_set "require.progs"  zfs
}
clone_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	verify_zvol_recursive
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/clone_001_pos.ksh || atf_fail "Testcase failed"
}
clone_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rollback_001_pos cleanup
rollback_001_pos_head()
{
	atf_set "descr" "Verify that a rollback to a previous snapshot succeeds."
	atf_set "require.progs"  zfs
}
rollback_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rollback_001_pos.ksh || atf_fail "Testcase failed"
}
rollback_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rollback_002_pos cleanup
rollback_002_pos_head()
{
	atf_set "descr" "Verify rollback is with respect to latest snapshot."
	atf_set "require.progs"  zfs
}
rollback_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rollback_002_pos.ksh || atf_fail "Testcase failed"
}
rollback_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rollback_003_pos cleanup
rollback_003_pos_head()
{
	atf_set "descr" "Verify rollback succeeds when there are nested file systems."
	atf_set "require.progs"  zfs
}
rollback_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rollback_003_pos.ksh || atf_fail "Testcase failed"
}
rollback_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_001_pos cleanup
snapshot_001_pos_head()
{
	atf_set "descr" "Verify a file system snapshot is identical to original."
	atf_set "require.progs"  zfs sum
}
snapshot_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_001_pos.ksh || atf_fail "Testcase failed"
}
snapshot_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_002_pos cleanup
snapshot_002_pos_head()
{
	atf_set "descr" "Verify an archive of a file system is identical toan archive of its snapshot."
	atf_set "require.progs"  zfs
}
snapshot_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_002_pos.ksh || atf_fail "Testcase failed"
}
snapshot_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_003_pos cleanup
snapshot_003_pos_head()
{
	atf_set "descr" "Verify many snapshots of a file system can be taken."
	atf_set "require.progs"  zfs
}
snapshot_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_003_pos.ksh || atf_fail "Testcase failed"
}
snapshot_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_004_pos cleanup
snapshot_004_pos_head()
{
	atf_set "descr" "Verify that a snapshot of an empty file system remains empty."
	atf_set "require.progs"  zfs
}
snapshot_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_004_pos.ksh || atf_fail "Testcase failed"
}
snapshot_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_005_pos cleanup
snapshot_005_pos_head()
{
	atf_set "descr" "Verify that a snapshot of a dataset is identical tothe original dataset."
	atf_set "require.progs"  zfs sum
}
snapshot_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_005_pos.ksh || atf_fail "Testcase failed"
}
snapshot_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_006_pos cleanup
snapshot_006_pos_head()
{
	atf_set "descr" "Verify that an archive of a dataset is identical toan archive of the dataset's snapshot."
	atf_set "require.progs"  zfs
}
snapshot_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_006_pos.ksh || atf_fail "Testcase failed"
}
snapshot_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_007_pos cleanup
snapshot_007_pos_head()
{
	atf_set "descr" "Verify that many snapshots can be made on a zfs dataset."
	atf_set "require.progs"  zfs
}
snapshot_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_007_pos.ksh || atf_fail "Testcase failed"
}
snapshot_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_008_pos cleanup
snapshot_008_pos_head()
{
	atf_set "descr" "Verify that destroying snapshots returns space to the pool."
	atf_set "require.progs"  zfs
}
snapshot_008_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_008_pos.ksh || atf_fail "Testcase failed"
}
snapshot_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_009_pos cleanup
snapshot_009_pos_head()
{
	atf_set "descr" "Verify snapshot -r can correctly create a snapshot tree."
	atf_set "require.progs"  zfs
}
snapshot_009_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_009_pos.ksh || atf_fail "Testcase failed"
}
snapshot_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_010_pos cleanup
snapshot_010_pos_head()
{
	atf_set "descr" "Verify 'destroy -r' can correctly destroy a snapshot subtree at any point."
	atf_set "require.progs"  zfs
}
snapshot_010_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_010_pos.ksh || atf_fail "Testcase failed"
}
snapshot_010_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_011_pos cleanup
snapshot_011_pos_head()
{
	atf_set "descr" "Verify that rollback to a snapshot created by snapshot -r succeeds."
	atf_set "require.progs"  zfs
}
snapshot_011_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_011_pos.ksh || atf_fail "Testcase failed"
}
snapshot_011_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_012_pos cleanup
snapshot_012_pos_head()
{
	atf_set "descr" "Verify that 'snapshot -r' can work with 'zfs promote'."
	atf_set "require.progs"  zfs
}
snapshot_012_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_012_pos.ksh || atf_fail "Testcase failed"
}
snapshot_012_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_013_pos cleanup
snapshot_013_pos_head()
{
	atf_set "descr" "Verify snapshots from 'snapshot -r' can be used for zfs send/recv"
	atf_set "require.progs"  zfs
}
snapshot_013_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_013_pos.ksh || atf_fail "Testcase failed"
}
snapshot_013_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_014_pos cleanup
snapshot_014_pos_head()
{
	atf_set "descr" "Verify creating/destroying snapshots do things clean"
	atf_set "require.progs"  zfs
}
snapshot_014_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_014_pos.ksh || atf_fail "Testcase failed"
}
snapshot_014_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_015_pos cleanup
snapshot_015_pos_head()
{
	atf_set "descr" "Verify snapshot can be created via mkdir in .zfs/snapshot."
	atf_set "require.progs"  zfs
}
snapshot_015_pos_body()
{
    atf_expect_fail "Not all directory operations on the .zfs/snapshot directory are yet supported by FreeBSD"
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_015_pos.ksh || atf_fail "Testcase failed"
}
snapshot_015_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_016_pos cleanup
snapshot_016_pos_head()
{
	atf_set "descr" "Verify renamed snapshots via mv can be destroyed."
	atf_set "require.progs"  zfs
}
snapshot_016_pos_body()
{
	atf_expect_fail "Not all directory operations on the .zfs/snapshot directory are yet supported by FreeBSD"
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_016_pos.ksh || atf_fail "Testcase failed"
}
snapshot_016_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_017_pos cleanup
snapshot_017_pos_head()
{
	atf_set "descr" "Directory structure of snapshots reflects filesystem structure."
	atf_set "require.progs"  zfs
}
snapshot_017_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_017_pos.ksh || atf_fail "Testcase failed"
}
snapshot_017_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_018_pos cleanup
snapshot_018_pos_head()
{
	atf_set "descr" "Snapshot directory supports ACL operations"
	atf_set "require.progs" zfs getfacl getconf sha1
}
snapshot_018_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_018_pos.ksh || atf_fail "Testcase failed"
}
snapshot_018_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapshot_019_pos cleanup
snapshot_019_pos_head()
{
	atf_set "descr" "Accessing snapshots and unmounting them in parallel does not panic"
	atf_set "require.progs" zfs
	atf_set "timeout" 1200
}
snapshot_019_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_019_pos.ksh || atf_fail "Testcase failed"
}
snapshot_019_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case snapshot_020_pos cleanup
snapshot_020_pos_head()
{
	atf_set "descr" "Verify mounted snapshots can be renamed and destroyed"
	atf_set "require.progs"  zfs
}
snapshot_020_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapshot_020_pos.ksh || atf_fail "Testcase failed"
}
snapshot_020_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{
	atf_add_test_case clone_001_pos
	atf_add_test_case rollback_001_pos
	atf_add_test_case rollback_002_pos
	atf_add_test_case rollback_003_pos
	atf_add_test_case snapshot_001_pos
	atf_add_test_case snapshot_002_pos
	atf_add_test_case snapshot_003_pos
	atf_add_test_case snapshot_004_pos
	atf_add_test_case snapshot_005_pos
	atf_add_test_case snapshot_006_pos
	atf_add_test_case snapshot_007_pos
	atf_add_test_case snapshot_008_pos
	atf_add_test_case snapshot_009_pos
	atf_add_test_case snapshot_010_pos
	atf_add_test_case snapshot_011_pos
	atf_add_test_case snapshot_012_pos
	atf_add_test_case snapshot_013_pos
	atf_add_test_case snapshot_014_pos
	atf_add_test_case snapshot_015_pos
	atf_add_test_case snapshot_016_pos
	atf_add_test_case snapshot_017_pos
	atf_add_test_case snapshot_018_pos
	atf_add_test_case snapshot_019_pos
	atf_add_test_case snapshot_020_pos
}
