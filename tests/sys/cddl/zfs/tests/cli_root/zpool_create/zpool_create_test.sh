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


atf_test_case zpool_create_001_pos cleanup
zpool_create_001_pos_head()
{
	atf_set "descr" "'zpool create <pool> <vspec> ...' can successfully createa new pool with a name in ZFS namespace."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_001_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_002_pos cleanup
zpool_create_002_pos_head()
{
	atf_set "descr" "'zpool create -f <pool> <vspec> ...' can successfully createa new pool in some cases."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_002_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_003_pos cleanup
zpool_create_003_pos_head()
{
	atf_set "descr" "'zpool create -n <pool> <vspec> ...' can display the configureationwithout actually creating the pool."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_003_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_004_pos cleanup
zpool_create_004_pos_head()
{
	atf_set "descr" "'zpool create [-f]' can create a storage pool with large numbers of vdevswithout any errors."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2400
}
zpool_create_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_004_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_005_pos cleanup
zpool_create_005_pos_head()
{
	atf_set "descr" "'zpool create [-R root][-m mountpoint] <pool> <vdev> ...' can createan alternate pool or a new pool mounted at the specified mountpoint."
	atf_set "require.progs"  zpool zfs
	atf_set "timeout" 2400
}
zpool_create_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_005_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_006_pos cleanup
zpool_create_006_pos_head()
{
	atf_set "descr" "Verify 'zpool create' succeed with keywords combination."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_006_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_007_neg cleanup
zpool_create_007_neg_head()
{
	atf_set "descr" "'zpool create' should return an error with badly-formed parameters."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_007_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_007_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_008_pos cleanup
zpool_create_008_pos_head()
{
	atf_set "descr" "'zpool create' have to use '-f' scenarios"
	atf_set "require.progs"  zpool format
	atf_set "timeout" 2400
}
zpool_create_008_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_008_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_009_neg cleanup
zpool_create_009_neg_head()
{
	atf_set "descr" "Create a pool with same devices twice or create two pools withsame devices, 'zpool create' should fail."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_009_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_009_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_010_neg cleanup
zpool_create_010_neg_head()
{
	atf_set "descr" "'zpool create' should return an error with VDEVs <64mb"
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2400
}
zpool_create_010_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_010_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_010_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_011_neg cleanup
zpool_create_011_neg_head()
{
	atf_set "descr" "'zpool create' should be failed with inapplicable scenarios."
	atf_set "require.progs"  dumpadm zpool
	atf_set "timeout" 2400
}
zpool_create_011_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_011_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_011_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_012_neg cleanup
zpool_create_012_neg_head()
{
	atf_set "descr" "'zpool create' should fail with disk slice in swap."
	atf_set "require.progs"  zpool swap
	atf_set "timeout" 2400
}
zpool_create_012_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_012_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_012_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_013_neg cleanup
zpool_create_013_neg_head()
{
	atf_set "descr" "'zpool create' should fail with metadevice in swap."
	atf_set "require.progs"  metadb metaclear metastat zpool metainit swap
	atf_set "timeout" 2400
}
zpool_create_013_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_013_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_013_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_014_neg cleanup
zpool_create_014_neg_head()
{
	atf_set "descr" "'zpool create' should fail with regular file in swap."
	atf_set "require.progs"  zfs swap zpool
	atf_set "timeout" 2400
}
zpool_create_014_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_014_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_014_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_015_neg cleanup
zpool_create_015_neg_head()
{
	atf_set "descr" "'zpool create' should fail with zfs vol device in swap."
	atf_set "require.progs"  zfs zpool swap
	atf_set "timeout" 2400
}
zpool_create_015_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_015_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_015_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_016_pos cleanup
zpool_create_016_pos_head()
{
	atf_set "descr" "'zpool create' should success with no device in swap."
	atf_set "require.progs"  dumpadm swapadd zpool swap
	atf_set "timeout" 2400
}
zpool_create_016_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_016_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_016_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_017_neg cleanup
zpool_create_017_neg_head()
{
	atf_set "descr" "'zpool create' should fail with mountpoint exists and not empty."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_017_neg_body()
{
	[ `uname -s` = "FreeBSD" ] && atf_skip "FreeBSD does not consider creating pools on non-empty mountpoints a bug"

	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_017_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_017_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_018_pos cleanup
zpool_create_018_pos_head()
{
	atf_set "descr" "zpool create can create pools with specified properties"
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_018_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_018_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_018_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_019_pos cleanup
zpool_create_019_pos_head()
{
	atf_set "descr" "zpool create cannot create pools specifying readonly properties"
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_019_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_019_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_019_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_020_pos cleanup
zpool_create_020_pos_head()
{
	atf_set "descr" "zpool create -R works as expected"
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2400
}
zpool_create_020_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_020_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_020_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_021_pos cleanup
zpool_create_021_pos_head()
{
	atf_set "descr" "'zpool create -O property=value pool' can successfully create a poolwith correct filesystem property set."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_021_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_021_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_021_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_022_pos cleanup
zpool_create_022_pos_head()
{
	atf_set "descr" "'zpool create -O property=value pool' can successfully create a poolwith multiple filesystem properties set."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_022_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_022_pos.ksh || atf_fail "Testcase failed"
}
zpool_create_022_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_023_neg cleanup
zpool_create_023_neg_head()
{
	atf_set "descr" "'zpool create -O' should return an error with badly formed parameters."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2400
}
zpool_create_023_neg_body()
{
	atf_expect_fail 'kern/221987 - ZFS does not validate the sharenfs parameter'
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_create_023_neg.ksh || atf_fail "Testcase failed"
}
zpool_create_023_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_create.kshlib
	. $(atf_get_srcdir)/zpool_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zpool_create_001_pos
	atf_add_test_case zpool_create_002_pos
	atf_add_test_case zpool_create_003_pos
	atf_add_test_case zpool_create_004_pos
	atf_add_test_case zpool_create_005_pos
	atf_add_test_case zpool_create_006_pos
	atf_add_test_case zpool_create_007_neg
	atf_add_test_case zpool_create_008_pos
	atf_add_test_case zpool_create_009_neg
	atf_add_test_case zpool_create_010_neg
	atf_add_test_case zpool_create_011_neg
	atf_add_test_case zpool_create_012_neg
	atf_add_test_case zpool_create_013_neg
	atf_add_test_case zpool_create_014_neg
	atf_add_test_case zpool_create_015_neg
	atf_add_test_case zpool_create_016_pos
	atf_add_test_case zpool_create_017_neg
	atf_add_test_case zpool_create_018_pos
	atf_add_test_case zpool_create_019_pos
	atf_add_test_case zpool_create_020_pos
	atf_add_test_case zpool_create_021_pos
	atf_add_test_case zpool_create_022_pos
	atf_add_test_case zpool_create_023_neg
}
