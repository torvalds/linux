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


atf_test_case zvol_misc_001_neg cleanup
zvol_misc_001_neg_head()
{
	atf_set "descr" "Verify that ZFS volume cannot act as dump device until dumpswap supported."
	atf_set "require.progs"  dumpadm
}
zvol_misc_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_misc_001_neg.ksh || atf_fail "Testcase failed"
}
zvol_misc_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_misc_002_pos cleanup
zvol_misc_002_pos_head()
{
	atf_set "descr" "Verify that ZFS volume snapshot could be fscked"
	atf_set "require.progs"  zfs
}
zvol_misc_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_misc_002_pos.ksh || atf_fail "Testcase failed"
}
zvol_misc_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_misc_003_neg cleanup
zvol_misc_003_neg_head()
{
	atf_set "descr" "Verify create storage pool or newfs over dump volume is denied."
	atf_set "require.progs"  dumpadm zpool
}
zvol_misc_003_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_misc_003_neg.ksh || atf_fail "Testcase failed"
}
zvol_misc_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_misc_004_pos cleanup
zvol_misc_004_pos_head()
{
	atf_set "descr" "Verify permit to create snapshot over dumpswap."
	atf_set "require.progs"  zfs swap
}
zvol_misc_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_misc_004_pos.ksh || atf_fail "Testcase failed"
}
zvol_misc_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_misc_005_neg cleanup
zvol_misc_005_neg_head()
{
	atf_set "descr" "Verify a device cannot be dump and swap at the same time."
	atf_set "require.progs"  dumpadm swap
}
zvol_misc_005_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_misc_005_neg.ksh || atf_fail "Testcase failed"
}
zvol_misc_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_misc_006_pos cleanup
zvol_misc_006_pos_head()
{
	atf_set "descr" "zfs volume as dumpdevice should have 128k volblocksize"
	atf_set "require.progs"  dumpadm zfs
}
zvol_misc_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_misc_006_pos.ksh || atf_fail "Testcase failed"
}
zvol_misc_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_misc_007_pos cleanup
zvol_misc_007_pos_head()
{
	atf_set "descr" "zfs volume device nodes are modified appropriately"
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
zvol_misc_007_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg
	atf_expect_fail "PR 225223 zfs rename -r of a snapshot doesn't rename zvol snapshots' device nodes"

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_misc_007_pos.ksh || atf_fail "Testcase failed"
}
zvol_misc_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_misc_008_pos cleanup
zvol_misc_008_pos_head()
{
	atf_set "descr" "zfs volume device nodes are modified appropriately"
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
zvol_misc_008_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg
	atf_expect_fail "PR 225200 zfs promote of a zvol doesn't rename device nodes for snapshots"

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_misc_008_pos.ksh || atf_fail "Testcase failed"
}
zvol_misc_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_misc_009_pos cleanup
zvol_misc_009_pos_head()
{
	atf_set "descr" "zfs volume device nodes are modified appropriately"
	atf_set "require.progs"  zfs
	atf_set "timeout" 1200
}
zvol_misc_009_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_misc_009_pos.ksh || atf_fail "Testcase failed"
}
zvol_misc_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zvol_misc_001_neg
	atf_add_test_case zvol_misc_002_pos
	atf_add_test_case zvol_misc_003_neg
	atf_add_test_case zvol_misc_004_pos
	atf_add_test_case zvol_misc_005_neg
	atf_add_test_case zvol_misc_006_pos
	atf_add_test_case zvol_misc_007_pos
	atf_add_test_case zvol_misc_008_pos
	atf_add_test_case zvol_misc_009_pos
}
