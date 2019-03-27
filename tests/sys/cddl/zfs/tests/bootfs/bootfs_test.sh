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


atf_test_case bootfs_001_pos
bootfs_001_pos_head()
{
	atf_set "descr" "Valid datasets are accepted as bootfs property values"
	atf_set "require.progs"  zpool zfs
}
bootfs_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/bootfs.cfg

	ksh93 $(atf_get_srcdir)/bootfs_001_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case bootfs_002_neg
bootfs_002_neg_head()
{
	atf_set "descr" "Invalid datasets are rejected as boot property values"
	atf_set "require.progs"  zfs zpool
}
bootfs_002_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/bootfs.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/bootfs_002_neg.ksh || atf_fail "Testcase failed"
}


atf_test_case bootfs_003_pos
bootfs_003_pos_head()
{
	atf_set "descr" "Valid pool names are accepted by zpool set bootfs"
	atf_set "require.progs"  zpool zfs
}
bootfs_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/bootfs.cfg

	ksh93 $(atf_get_srcdir)/bootfs_003_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case bootfs_004_neg
bootfs_004_neg_head()
{
	atf_set "descr" "Invalid pool names are rejected by zpool set bootfs"
	atf_set "require.progs"  zpool zfs
}
bootfs_004_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/bootfs.cfg

	ksh93 $(atf_get_srcdir)/bootfs_004_neg.ksh || atf_fail "Testcase failed"
}


atf_test_case bootfs_005_neg
bootfs_005_neg_head()
{
	atf_set "descr" "Boot properties cannot be set on pools with older versions"
	atf_set "require.progs"  zfs zpool
}
bootfs_005_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/bootfs.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/bootfs_005_neg.ksh || atf_fail "Testcase failed"
}


atf_test_case bootfs_006_pos
bootfs_006_pos_head()
{
	atf_set "descr" "Pools of correct vdev types accept boot property"
	atf_set "require.progs"  zfs zpool
}
bootfs_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/bootfs.cfg

	ksh93 $(atf_get_srcdir)/bootfs_006_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case bootfs_007_pos
bootfs_007_pos_head()
{
	atf_set "descr" "setting bootfs on a pool which was configured with the whole disk will succeed"
	atf_set "require.progs"  zfs zpool
}
bootfs_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/bootfs.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/bootfs_007_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case bootfs_008_neg
bootfs_008_neg_head()
{
	atf_set "descr" "setting bootfs on a dataset which has gzip compression enabled will fail"
	atf_set "require.progs"  zpool zfs
}
bootfs_008_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/bootfs.cfg

	ksh93 $(atf_get_srcdir)/bootfs_008_neg.ksh || atf_fail "Testcase failed"
}


atf_test_case bootfs_009_neg
bootfs_009_neg_head()
{
	atf_set "descr" "Valid encrypted datasets can't be set bootfs property values"
	atf_set "require.config" zfs_encryption
	atf_set "require.progs"  zfs zpool
}
bootfs_009_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/bootfs.cfg

	ksh93 $(atf_get_srcdir)/bootfs_009_neg.ksh || atf_fail "Testcase failed"
}


atf_init_test_cases()
{

	atf_add_test_case bootfs_001_pos
	atf_add_test_case bootfs_002_neg
	atf_add_test_case bootfs_003_pos
	atf_add_test_case bootfs_004_neg
	atf_add_test_case bootfs_005_neg
	atf_add_test_case bootfs_006_pos
	atf_add_test_case bootfs_007_pos
	atf_add_test_case bootfs_008_neg
	atf_add_test_case bootfs_009_neg
}
