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


atf_test_case cache_001_pos cleanup
cache_001_pos_head()
{
	atf_set "descr" "Setting a valid {primary|secondary}cache on file system and volume,It should be successful."
	atf_set "timeout" 1200
}
cache_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_001_pos.ksh || atf_fail "Testcase failed"
}
cache_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_002_neg cleanup
cache_002_neg_head()
{
	atf_set "descr" "Setting invalid {primary|secondary}cache on fs and volume,It should fail."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
cache_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_002_neg.ksh || atf_fail "Testcase failed"
}
cache_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case canmount_001_pos cleanup
canmount_001_pos_head()
{
	atf_set "descr" "Setting a valid property of canmount to file system, it must be successful."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
canmount_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/canmount_001_pos.ksh || atf_fail "Testcase failed"
}
canmount_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case canmount_002_pos cleanup
canmount_002_pos_head()
{
	atf_set "descr" "Setting canmount=noauto to file system, it must be successful."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
canmount_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	if other_pools_exist; then
                atf_skip "Can't test unmount -a with existing pools"
        fi

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/canmount_002_pos.ksh || atf_fail "Testcase failed"
}
canmount_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case canmount_003_pos cleanup
canmount_003_pos_head()
{
	atf_set "descr" "While canmount=noauto and  the dataset is mounted, zfs must not attempt to unmount it"
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
canmount_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	if other_pools_exist; then
                atf_skip "Can't test unmount -a with existing pools"
        fi

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/canmount_003_pos.ksh || atf_fail "Testcase failed"
}
canmount_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case canmount_004_pos cleanup
canmount_004_pos_head()
{
	atf_set "descr" "Verify canmount=noauto work fine when setting sharenfs or sharesmb."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
canmount_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/canmount_004_pos.ksh || atf_fail "Testcase failed"
}
canmount_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case checksum_001_pos cleanup
checksum_001_pos_head()
{
	atf_set "descr" "Setting a valid checksum on a file system, volume,it should be successful."
	atf_set "timeout" 1200
}
checksum_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/checksum_001_pos.ksh || atf_fail "Testcase failed"
}
checksum_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case compression_001_pos cleanup
compression_001_pos_head()
{
	atf_set "descr" "Setting a valid compression on file system and volume,It should be successful."
	atf_set "timeout" 1200
}
compression_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/compression_001_pos.ksh || atf_fail "Testcase failed"
}
compression_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case mountpoint_001_pos cleanup
mountpoint_001_pos_head()
{
	atf_set "descr" "Setting a valid mountpoint to file system, it must be successful."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
mountpoint_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/mountpoint_001_pos.ksh || atf_fail "Testcase failed"
}
mountpoint_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case mountpoint_002_pos cleanup
mountpoint_002_pos_head()
{
	atf_set "descr" "Setting a valid mountpoint for an unmounted file system,it remains unmounted."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
mountpoint_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/mountpoint_002_pos.ksh || atf_fail "Testcase failed"
}
mountpoint_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case mountpoint_003_pos cleanup
mountpoint_003_pos_head()
{
	atf_set "descr" "With legacy mount, FSType-specific option works well."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
mountpoint_003_pos_body()
{
	atf_expect_fail "The devices property is not yet supported on FreeBSD"
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/mountpoint_003_pos.ksh || atf_fail "Testcase failed"
}
mountpoint_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case onoffs_001_pos cleanup
onoffs_001_pos_head()
{
	atf_set "descr" "Setting a valid value to atime, readonly, setuid or zoned on filesystem or volume. It should be successful."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
onoffs_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/onoffs_001_pos.ksh || atf_fail "Testcase failed"
}
onoffs_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case property_alias_001_pos cleanup
property_alias_001_pos_head()
{
	atf_set "descr" "Properties with aliases also work with those aliases."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
property_alias_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/property_alias_001_pos.ksh || atf_fail "Testcase failed"
}
property_alias_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case readonly_001_pos cleanup
readonly_001_pos_head()
{
	atf_set "descr" "Setting a valid readonly property on a dataset succeeds."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
readonly_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/readonly_001_pos.ksh || atf_fail "Testcase failed"
}
readonly_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_001_neg cleanup
reservation_001_neg_head()
{
	atf_set "descr" "Verify invalid reservation values are rejected"
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
reservation_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_001_neg.ksh || atf_fail "Testcase failed"
}
reservation_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case ro_props_001_pos cleanup
ro_props_001_pos_head()
{
	atf_set "descr" "Verify that read-only properties are immutable."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
ro_props_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/ro_props_001_pos.ksh || atf_fail "Testcase failed"
}
ro_props_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case share_mount_001_neg cleanup
share_mount_001_neg_head()
{
	atf_set "descr" "Verify that we cannot share or mount legacy filesystems."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
share_mount_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/share_mount_001_neg.ksh || atf_fail "Testcase failed"
}
share_mount_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case snapdir_001_pos cleanup
snapdir_001_pos_head()
{
	atf_set "descr" "Setting a valid snapdir property on a dataset succeeds."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
snapdir_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/snapdir_001_pos.ksh || atf_fail "Testcase failed"
}
snapdir_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case user_property_001_pos cleanup
user_property_001_pos_head()
{
	atf_set "descr" "ZFS can set any valid user defined property to the non-readonlydataset."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
user_property_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/user_property_001_pos.ksh || atf_fail "Testcase failed"
}
user_property_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case user_property_002_pos cleanup
user_property_002_pos_head()
{
	atf_set "descr" "User defined property inherited from its parent."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
user_property_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/user_property_002_pos.ksh || atf_fail "Testcase failed"
}
user_property_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case user_property_003_neg cleanup
user_property_003_neg_head()
{
	atf_set "descr" "ZFS can handle invalid user property."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
user_property_003_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/user_property_003_neg.ksh || atf_fail "Testcase failed"
}
user_property_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case user_property_004_pos cleanup
user_property_004_pos_head()
{
	atf_set "descr" "User property has no effect to snapshot until 'Snapshot properties' supported."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1200
}
user_property_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/user_property_004_pos.ksh || atf_fail "Testcase failed"
}
user_property_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case version_001_neg cleanup
version_001_neg_head()
{
	atf_set "descr" "Verify invalid version values are rejected"
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
version_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/version_001_neg.ksh || atf_fail "Testcase failed"
}
version_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_set_001_neg cleanup
zfs_set_001_neg_head()
{
	atf_set "descr" "Setting invalid value to mountpoint, checksum, compression, atime,readonly, setuid, zoned or canmount on a file system file system or volume. \It should be failed."
	atf_set "timeout" 1200
}
zfs_set_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_set_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_set_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_set_002_neg cleanup
zfs_set_002_neg_head()
{
	atf_set "descr" "'zfs set' fails with invalid arguments"
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
zfs_set_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_set_002_neg.ksh || atf_fail "Testcase failed"
}
zfs_set_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_set_003_neg cleanup
zfs_set_003_neg_head()
{
	atf_set "descr" "'zfs set mountpoint/sharenfs' fails with invalid scenarios"
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
zfs_set_003_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_set_003_neg.ksh || atf_fail "Testcase failed"
}
zfs_set_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_set_common.kshlib
	. $(atf_get_srcdir)/zfs_set.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case cache_001_pos
	atf_add_test_case cache_002_neg
	atf_add_test_case canmount_001_pos
	atf_add_test_case canmount_002_pos
	atf_add_test_case canmount_003_pos
	atf_add_test_case canmount_004_pos
	atf_add_test_case checksum_001_pos
	atf_add_test_case compression_001_pos
	atf_add_test_case mountpoint_001_pos
	atf_add_test_case mountpoint_002_pos
	atf_add_test_case mountpoint_003_pos
	atf_add_test_case onoffs_001_pos
	atf_add_test_case property_alias_001_pos
	atf_add_test_case readonly_001_pos
	atf_add_test_case reservation_001_neg
	atf_add_test_case ro_props_001_pos
	atf_add_test_case share_mount_001_neg
	atf_add_test_case snapdir_001_pos
	atf_add_test_case user_property_001_pos
	atf_add_test_case user_property_002_pos
	atf_add_test_case user_property_003_neg
	atf_add_test_case user_property_004_pos
	atf_add_test_case version_001_neg
	atf_add_test_case zfs_set_001_neg
	atf_add_test_case zfs_set_002_neg
	atf_add_test_case zfs_set_003_neg
}
