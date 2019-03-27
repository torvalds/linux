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


atf_test_case zfs_create_001_pos cleanup
zfs_create_001_pos_head()
{
	atf_set "descr" "'zfs create <filesystem>' can create a ZFS filesystem in the namespace."
	atf_set "require.progs"  zfs
}
zfs_create_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_002_pos cleanup
zfs_create_002_pos_head()
{
	atf_set "descr" "'zfs create -s -V <size> <volume>' succeeds"
	atf_set "require.progs"  zfs
}
zfs_create_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_003_pos cleanup
zfs_create_003_pos_head()
{
	atf_set "descr" "Verify creating volume with specified blocksize works."
	atf_set "require.progs"  zfs
}
zfs_create_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_004_pos cleanup
zfs_create_004_pos_head()
{
	atf_set "descr" "'zfs create -o property=value filesystem' can successfully createa ZFS filesystem with correct property set."
	atf_set "require.progs"  zfs
}
zfs_create_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_005_pos cleanup
zfs_create_005_pos_head()
{
	atf_set "descr" "'zfs create -o property=value filesystem' can successfully createa ZFS filesystem with multiple properties set."
	atf_set "require.progs"  zfs
}
zfs_create_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_006_pos cleanup
zfs_create_006_pos_head()
{
	atf_set "descr" "'zfs create -o property=value -V size volume' can successfullycreate a ZFS volume with correct property set."
	atf_set "require.progs"  zfs
}
zfs_create_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_007_pos cleanup
zfs_create_007_pos_head()
{
	atf_set "descr" "'zfs create -o property=value -V size volume' can successfullycreate a ZFS volume with correct property set."
	atf_set "require.progs"  zfs
}
zfs_create_007_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_007_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_008_neg cleanup
zfs_create_008_neg_head()
{
	atf_set "descr" "'zfs create' should return an error with badly-formed parameters."
	atf_set "require.progs"  zfs
}
zfs_create_008_neg_body()
{
	atf_expect_fail 'kern/221987 - ZFS does not validate the sharenfs parameter'
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_008_neg.ksh || atf_fail "Testcase failed"
}
zfs_create_008_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_009_neg cleanup
zfs_create_009_neg_head()
{
	atf_set "descr" "Verify 'zfs create <filesystem>' fails with bad <filesystem> argument."
	atf_set "require.progs"  zfs
}
zfs_create_009_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_009_neg.ksh || atf_fail "Testcase failed"
}
zfs_create_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_010_neg cleanup
zfs_create_010_neg_head()
{
	atf_set "descr" "Verify 'zfs create [-s] [-b <blocksize> ] -V <size> <volume>' fails withbadly-formed <size> or <volume> arguments."
	atf_set "require.progs"  zfs
}
zfs_create_010_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_010_neg.ksh || atf_fail "Testcase failed"
}
zfs_create_010_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_011_pos cleanup
zfs_create_011_pos_head()
{
	atf_set "descr" "'zfs create -p' works as expected."
	atf_set "require.progs"  zfs
}
zfs_create_011_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_011_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_011_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_012_pos cleanup
zfs_create_012_pos_head()
{
	atf_set "descr" "'zfs create -p -o version=1' only cause the leaf filesystem to be version=1."
	atf_set "require.progs"  zfs
}
zfs_create_012_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_012_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_012_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_013_pos cleanup
zfs_create_013_pos_head()
{
	atf_set "descr" "'zfs create -s -V <size> <volume>' succeeds"
	atf_set "require.progs"  zfs
}
zfs_create_013_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_013_pos.ksh || atf_fail "Testcase failed"
}
zfs_create_013_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_create_common.kshlib
	. $(atf_get_srcdir)/properties.kshlib
	. $(atf_get_srcdir)/zfs_create.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_create_001_pos
	atf_add_test_case zfs_create_002_pos
	atf_add_test_case zfs_create_003_pos
	atf_add_test_case zfs_create_004_pos
	atf_add_test_case zfs_create_005_pos
	atf_add_test_case zfs_create_006_pos
	atf_add_test_case zfs_create_007_pos
	atf_add_test_case zfs_create_008_neg
	atf_add_test_case zfs_create_009_neg
	atf_add_test_case zfs_create_010_neg
	atf_add_test_case zfs_create_011_pos
	atf_add_test_case zfs_create_012_pos
	atf_add_test_case zfs_create_013_pos
}
