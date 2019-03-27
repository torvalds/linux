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


atf_test_case groupspace_001_pos cleanup
groupspace_001_pos_head()
{
	atf_set "descr" "Check the zfs groupspace with all possible parameters"
	atf_set "require.progs"  zfs runwattr
}
groupspace_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/groupspace_001_pos.ksh || atf_fail "Testcase failed"
}
groupspace_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case groupspace_002_pos cleanup
groupspace_002_pos_head()
{
	atf_set "descr" "Check the zfs groupspace used and quota"
	atf_set "require.progs"  zfs runwattr
}
groupspace_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/groupspace_002_pos.ksh || atf_fail "Testcase failed"
}
groupspace_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_001_pos cleanup
userquota_001_pos_head()
{
	atf_set "descr" "If write operation overwrite {user|group}quota size, it will fail"
	atf_set "require.progs"  zfs runwattr
}
userquota_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_001_pos.ksh || atf_fail "Testcase failed"
}
userquota_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_002_pos cleanup
userquota_002_pos_head()
{
	atf_set "descr" "the userquota and groupquota can be set during zpool,zfs creation"
	atf_set "require.progs"  zpool zfs
}
userquota_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_002_pos.ksh || atf_fail "Testcase failed"
}
userquota_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_003_pos cleanup
userquota_003_pos_head()
{
	atf_set "descr" "Check the basic function of set/get userquota and groupquota on fs"
	atf_set "require.progs"  zfs
}
userquota_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_003_pos.ksh || atf_fail "Testcase failed"
}
userquota_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_004_pos cleanup
userquota_004_pos_head()
{
	atf_set "descr" "Check the basic function of {user|group} used"
	atf_set "require.progs"  runwattr
}
userquota_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_004_pos.ksh || atf_fail "Testcase failed"
}
userquota_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_005_neg cleanup
userquota_005_neg_head()
{
	atf_set "descr" "Check the invalid parameter of zfs set user|group quota"
	atf_set "require.progs"  zfs
}
userquota_005_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_005_neg.ksh || atf_fail "Testcase failed"
}
userquota_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_006_pos cleanup
userquota_006_pos_head()
{
	atf_set "descr" "Check the invalid parameter of zfs get user|group quota"
	atf_set "require.progs"  zfs
}
userquota_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_006_pos.ksh || atf_fail "Testcase failed"
}
userquota_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_007_pos cleanup
userquota_007_pos_head()
{
	atf_set "descr" "Check set user|group quota to larger than the quota size of a fs"
	atf_set "require.progs"  zfs runwattr
}
userquota_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_007_pos.ksh || atf_fail "Testcase failed"
}
userquota_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_008_pos cleanup
userquota_008_pos_head()
{
	atf_set "descr" "Check zfs get all will not print out user|group quota"
	atf_set "require.progs"  zfs
}
userquota_008_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_008_pos.ksh || atf_fail "Testcase failed"
}
userquota_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_009_pos cleanup
userquota_009_pos_head()
{
	atf_set "descr" "Check the snapshot's user|group quota"
	atf_set "require.progs"  zfs
}
userquota_009_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_009_pos.ksh || atf_fail "Testcase failed"
}
userquota_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_010_pos cleanup
userquota_010_pos_head()
{
	atf_set "descr" "overwrite any of the {user|group}quota size, it will fail"
	atf_set "require.progs"  zfs runwattr
}
userquota_010_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_010_pos.ksh || atf_fail "Testcase failed"
}
userquota_010_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_011_pos cleanup
userquota_011_pos_head()
{
	atf_set "descr" "the userquota and groupquota can't change during zfs actions"
	atf_set "require.progs"  zfs
}
userquota_011_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_011_pos.ksh || atf_fail "Testcase failed"
}
userquota_011_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userquota_012_neg cleanup
userquota_012_neg_head()
{
	atf_set "descr" "Check  set userquota and groupquota on snapshot"
	atf_set "require.progs"  zfs
}
userquota_012_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userquota_012_neg.ksh || atf_fail "Testcase failed"
}
userquota_012_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userspace_001_pos cleanup
userspace_001_pos_head()
{
	atf_set "descr" "Check the zfs userspace with all possible parameters"
	atf_set "require.progs"  zfs runwattr
}
userspace_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userspace_001_pos.ksh || atf_fail "Testcase failed"
}
userspace_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case userspace_002_pos cleanup
userspace_002_pos_head()
{
	atf_set "descr" "Check the zfs userspace used and quota"
	atf_set "require.progs"  zfs runwattr
}
userspace_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/userspace_002_pos.ksh || atf_fail "Testcase failed"
}
userspace_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/userquota_common.kshlib
	. $(atf_get_srcdir)/userquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case groupspace_001_pos
	atf_add_test_case groupspace_002_pos
	atf_add_test_case userquota_001_pos
	atf_add_test_case userquota_002_pos
	atf_add_test_case userquota_003_pos
	atf_add_test_case userquota_004_pos
	atf_add_test_case userquota_005_neg
	atf_add_test_case userquota_006_pos
	atf_add_test_case userquota_007_pos
	atf_add_test_case userquota_008_pos
	atf_add_test_case userquota_009_pos
	atf_add_test_case userquota_010_pos
	atf_add_test_case userquota_011_pos
	atf_add_test_case userquota_012_neg
	atf_add_test_case userspace_001_pos
	atf_add_test_case userspace_002_pos
}
