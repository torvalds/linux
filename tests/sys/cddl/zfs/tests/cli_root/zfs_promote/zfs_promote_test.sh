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


atf_test_case zfs_promote_001_pos cleanup
zfs_promote_001_pos_head()
{
	atf_set "descr" "'zfs promote' can promote a clone filesystem."
	atf_set "require.progs"  zfs
}
zfs_promote_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_promote_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_promote_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_promote_002_pos cleanup
zfs_promote_002_pos_head()
{
	atf_set "descr" "'zfs promote' can deal with multiple snapshots in a filesystem."
	atf_set "require.progs"  zfs
}
zfs_promote_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_promote_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_promote_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_promote_003_pos cleanup
zfs_promote_003_pos_head()
{
	atf_set "descr" "'zfs promote' can deal with multi-point snapshots."
	atf_set "require.progs"  zfs
}
zfs_promote_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_promote_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_promote_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_promote_004_pos cleanup
zfs_promote_004_pos_head()
{
	atf_set "descr" "'zfs promote' can deal with multi-level clone."
	atf_set "require.progs"  zfs
}
zfs_promote_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_promote_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_promote_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_promote_005_pos cleanup
zfs_promote_005_pos_head()
{
	atf_set "descr" "The original fs was unmounted, 'zfs promote' still should succeed."
	atf_set "require.progs"  zfs
}
zfs_promote_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_promote_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_promote_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_promote_006_neg cleanup
zfs_promote_006_neg_head()
{
	atf_set "descr" "'zfs promote' will fail with invalid arguments."
	atf_set "require.progs"  zfs
}
zfs_promote_006_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_promote_006_neg.ksh || atf_fail "Testcase failed"
}
zfs_promote_006_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_promote_007_neg cleanup
zfs_promote_007_neg_head()
{
	atf_set "descr" "'zfs promote' can deal with name conflicts."
	atf_set "require.progs"  zfs
}
zfs_promote_007_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_promote_007_neg.ksh || atf_fail "Testcase failed"
}
zfs_promote_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_promote_008_pos cleanup
zfs_promote_008_pos_head()
{
	atf_set "descr" "'zfs promote' can promote a volume clone."
	atf_set "require.progs"  zfs
}
zfs_promote_008_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_promote_008_pos.ksh || atf_fail "Testcase failed"
}
zfs_promote_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_promote_common.kshlib
	. $(atf_get_srcdir)/zfs_promote.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_promote_001_pos
	atf_add_test_case zfs_promote_002_pos
	atf_add_test_case zfs_promote_003_pos
	atf_add_test_case zfs_promote_004_pos
	atf_add_test_case zfs_promote_005_pos
	atf_add_test_case zfs_promote_006_neg
	atf_add_test_case zfs_promote_007_neg
	atf_add_test_case zfs_promote_008_pos
}
