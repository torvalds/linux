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


atf_test_case zfs_share_001_pos cleanup
zfs_share_001_pos_head()
{
	atf_set "descr" "Verify that 'zfs share' succeeds as root."
	atf_set "require.progs"  zfs svcs
}
zfs_share_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_share_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_002_pos cleanup
zfs_share_002_pos_head()
{
	atf_set "descr" "Verify that zfs share with a non-existent file system fails."
	atf_set "require.progs"  zfs svcs
}
zfs_share_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_share_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_003_pos cleanup
zfs_share_003_pos_head()
{
	atf_set "descr" "Verify that '$ZFS share' with a file systemwhose sharenfs property is 'off'   \will fail with return code 1."
	atf_set "require.progs"  zfs svcs
}
zfs_share_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_share_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_004_pos cleanup
zfs_share_004_pos_head()
{
	atf_set "descr" "Verify that a file system and its snapshot are shared."
	atf_set "require.progs"  zfs svcs
}
zfs_share_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_share_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_005_pos cleanup
zfs_share_005_pos_head()
{
	atf_set "descr" "Verify that NFS share options are propagated correctly."
	atf_set "require.progs"  zfs share svcs
}
zfs_share_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_share_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_006_pos cleanup
zfs_share_006_pos_head()
{
	atf_set "descr" "Verify that a dataset could not be shared,but its sub-filesystems could be shared."
	atf_set "require.progs"  zfs svcs
}
zfs_share_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_share_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_007_neg cleanup
zfs_share_007_neg_head()
{
	atf_set "descr" "Verify that invalid share parameters and options are caught."
	atf_set "require.progs"  zfs share svcs
}
zfs_share_007_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_007_neg.ksh || atf_fail "Testcase failed"
}
zfs_share_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_008_neg cleanup
zfs_share_008_neg_head()
{
	atf_set "descr" "Verify that sharing a dataset other than filesystem fails."
	atf_set "require.progs"  zfs svcs
}
zfs_share_008_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_008_neg.ksh || atf_fail "Testcase failed"
}
zfs_share_008_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_009_neg cleanup
zfs_share_009_neg_head()
{
	atf_set "descr" "zfs share fails with shared filesystem"
	atf_set "require.progs"  zfs share svcs
}
zfs_share_009_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_009_neg.ksh || atf_fail "Testcase failed"
}
zfs_share_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_009_pos cleanup
zfs_share_009_pos_head()
{
	atf_set "descr" "Verify umount/rollback/destroy fails does not unshare the sharedfile system"
	atf_set "require.progs"  zfs svcs
}
zfs_share_009_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_009_pos.ksh || atf_fail "Testcase failed"
}
zfs_share_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_010_neg cleanup
zfs_share_010_neg_head()
{
	atf_set "descr" "zfs share fails with bad parameters"
	atf_set "require.progs"  zfs svcs
}
zfs_share_010_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_010_neg.ksh || atf_fail "Testcase failed"
}
zfs_share_010_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_share.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_share_001_pos
	atf_add_test_case zfs_share_002_pos
	atf_add_test_case zfs_share_003_pos
	atf_add_test_case zfs_share_004_pos
	atf_add_test_case zfs_share_005_pos
	atf_add_test_case zfs_share_006_pos
	atf_add_test_case zfs_share_007_neg
	atf_add_test_case zfs_share_008_neg
	atf_add_test_case zfs_share_009_neg
	atf_add_test_case zfs_share_009_pos
	atf_add_test_case zfs_share_010_neg
}
