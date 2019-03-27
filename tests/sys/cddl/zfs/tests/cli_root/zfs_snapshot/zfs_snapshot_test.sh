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


atf_test_case zfs_snapshot_001_neg cleanup
zfs_snapshot_001_neg_head()
{
	atf_set "descr" "Badly-formed 'zfs snapshot' with inapplicable scenariosshould return an error."
	atf_set "require.progs"  zfs
}
zfs_snapshot_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_snapshot_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_snapshot_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_snapshot_002_neg cleanup
zfs_snapshot_002_neg_head()
{
	atf_set "descr" "'zfs snapshot -r' fails with invalid arguments or scenarios."
	atf_set "require.progs"  zfs
}
zfs_snapshot_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_snapshot_002_neg.ksh || atf_fail "Testcase failed"
}
zfs_snapshot_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_snapshot_003_neg cleanup
zfs_snapshot_003_neg_head()
{
	atf_set "descr" "'zfs snapshot' fails with bad options, or too many arguments."
	atf_set "require.progs"  zfs
}
zfs_snapshot_003_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_snapshot_003_neg.ksh || atf_fail "Testcase failed"
}
zfs_snapshot_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_snapshot_004_neg cleanup
zfs_snapshot_004_neg_head()
{
	atf_set "descr" "Verify recursive snapshotting could not break ZFS."
	atf_set "require.progs"  zfs
}
zfs_snapshot_004_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_snapshot_004_neg.ksh || atf_fail "Testcase failed"
}
zfs_snapshot_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_snapshot_005_neg cleanup
zfs_snapshot_005_neg_head()
{
	atf_set "descr" "Verify long name filesystem with snapshot should not break ZFS."
	atf_set "require.progs"  zfs
}
zfs_snapshot_005_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_snapshot_005_neg.ksh || atf_fail "Testcase failed"
}
zfs_snapshot_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_snapshot_006_pos cleanup
zfs_snapshot_006_pos_head()
{
	atf_set "descr" "User property could be set upon snapshot via 'zfs snapshot -o'."
	atf_set "require.progs"  zfs zpool
}
zfs_snapshot_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_snapshot_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_snapshot_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_snapshot_007_neg cleanup
zfs_snapshot_007_neg_head()
{
	atf_set "descr" "'zfs snapshot -o' cannot set properties other than user property."
	atf_set "require.progs"  zfs
}
zfs_snapshot_007_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_snapshot_007_neg.ksh || atf_fail "Testcase failed"
}
zfs_snapshot_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_snapshot.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_snapshot_001_neg
	atf_add_test_case zfs_snapshot_002_neg
	atf_add_test_case zfs_snapshot_003_neg
	atf_add_test_case zfs_snapshot_004_neg
	atf_add_test_case zfs_snapshot_005_neg
	atf_add_test_case zfs_snapshot_006_pos
	atf_add_test_case zfs_snapshot_007_neg
}
