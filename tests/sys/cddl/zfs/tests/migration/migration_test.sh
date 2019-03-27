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


atf_test_case migration_001_pos cleanup
migration_001_pos_head()
{
	atf_set "descr" "Migrating test file from ZFS fs to ZFS fs using tar"
	atf_set "require.progs"  zfs
}
migration_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_001_pos.ksh || atf_fail "Testcase failed"
}
migration_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_002_pos cleanup
migration_002_pos_head()
{
	atf_set "descr" "Migrating test file from ZFS fs to UFS fs using tar"
	atf_set "require.progs"  zfs
}
migration_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_002_pos.ksh || atf_fail "Testcase failed"
}
migration_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_003_pos cleanup
migration_003_pos_head()
{
	atf_set "descr" "Migrating test file from UFS fs to ZFS fs using tar"
	atf_set "require.progs"  zfs
}
migration_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_003_pos.ksh || atf_fail "Testcase failed"
}
migration_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_004_pos cleanup
migration_004_pos_head()
{
	atf_set "descr" "Migrating test file from ZFS fs to ZFS fs using cpio"
	atf_set "require.progs"  zfs
}
migration_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_004_pos.ksh || atf_fail "Testcase failed"
}
migration_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_005_pos cleanup
migration_005_pos_head()
{
	atf_set "descr" "Migrating test file from ZFS fs to uFS fs using cpio"
	atf_set "require.progs"  zfs
}
migration_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_005_pos.ksh || atf_fail "Testcase failed"
}
migration_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_006_pos cleanup
migration_006_pos_head()
{
	atf_set "descr" "Migrating test file from UFS fs to ZFS fs using cpio"
	atf_set "require.progs"  zfs
}
migration_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_006_pos.ksh || atf_fail "Testcase failed"
}
migration_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_007_pos cleanup
migration_007_pos_head()
{
	atf_set "descr" "Migrating test file from ZFS fs to ZFS fs using dd"
	atf_set "require.progs"  zfs
}
migration_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_007_pos.ksh || atf_fail "Testcase failed"
}
migration_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_008_pos cleanup
migration_008_pos_head()
{
	atf_set "descr" "Migrating test file from ZFS fs to UFS fs using dd"
	atf_set "require.progs"  zfs
}
migration_008_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_008_pos.ksh || atf_fail "Testcase failed"
}
migration_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_009_pos cleanup
migration_009_pos_head()
{
	atf_set "descr" "Migrating test file from UFS fs to ZFS fs using dd"
	atf_set "require.progs"  zfs
}
migration_009_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_009_pos.ksh || atf_fail "Testcase failed"
}
migration_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_010_pos cleanup
migration_010_pos_head()
{
	atf_set "descr" "Migrating test file from ZFS fs to ZFS fs using cp"
	atf_set "require.progs"  zfs
}
migration_010_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_010_pos.ksh || atf_fail "Testcase failed"
}
migration_010_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_011_pos cleanup
migration_011_pos_head()
{
	atf_set "descr" "Migrating test file from ZFS fs to UFS fs using cp"
	atf_set "require.progs"  zfs
}
migration_011_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_011_pos.ksh || atf_fail "Testcase failed"
}
migration_011_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case migration_012_pos cleanup
migration_012_pos_head()
{
	atf_set "descr" "Migrating test file from UFS fs to ZFS fs using cp"
	atf_set "require.progs"  zfs
}
migration_012_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/migration_012_pos.ksh || atf_fail "Testcase failed"
}
migration_012_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/migration.kshlib
	. $(atf_get_srcdir)/migration.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case migration_001_pos
	atf_add_test_case migration_002_pos
	atf_add_test_case migration_003_pos
	atf_add_test_case migration_004_pos
	atf_add_test_case migration_005_pos
	atf_add_test_case migration_006_pos
	atf_add_test_case migration_007_pos
	atf_add_test_case migration_008_pos
	atf_add_test_case migration_009_pos
	atf_add_test_case migration_010_pos
	atf_add_test_case migration_011_pos
	atf_add_test_case migration_012_pos
}
