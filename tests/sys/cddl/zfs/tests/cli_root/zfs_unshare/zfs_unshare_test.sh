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


atf_test_case zfs_unshare_001_pos cleanup
zfs_unshare_001_pos_head()
{
	atf_set "descr" "Verify that 'zfs unshare [-a] <filesystem|mountpoint>' succeeds as root."
	atf_set "require.progs"  zfs unshare svcs
}
zfs_unshare_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unshare_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_unshare_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unshare_002_pos cleanup
zfs_unshare_002_pos_head()
{
	atf_set "descr" "Verify that 'zfs unshare [-a]' is aware of legacy share."
	atf_set "require.progs"  zfs unshare share svcs
}
zfs_unshare_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unshare_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_unshare_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unshare_003_pos cleanup
zfs_unshare_003_pos_head()
{
	atf_set "descr" "Verify that a file system and its dependent are unshared."
	atf_set "require.progs"  zfs unshare svcs
}
zfs_unshare_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unshare_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_unshare_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unshare_004_neg cleanup
zfs_unshare_004_neg_head()
{
	atf_set "descr" "Verify that '$ZFS unshare' issue error message with badly formed parameter."
	atf_set "require.progs"  zfs svcs
}
zfs_unshare_004_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unshare_004_neg.ksh || atf_fail "Testcase failed"
}
zfs_unshare_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unshare_005_neg cleanup
zfs_unshare_005_neg_head()
{
	atf_set "descr" "Verify that unsharing a dataset other than filesystem fails."
	atf_set "require.progs"  zfs svcs
}
zfs_unshare_005_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unshare_005_neg.ksh || atf_fail "Testcase failed"
}
zfs_unshare_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unshare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_unshare_001_pos
	atf_add_test_case zfs_unshare_002_pos
	atf_add_test_case zfs_unshare_003_pos
	atf_add_test_case zfs_unshare_004_neg
	atf_add_test_case zfs_unshare_005_neg
}
