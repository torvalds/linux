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


atf_test_case zpool_clear_001_pos cleanup
zpool_clear_001_pos_head()
{
	atf_set "descr" "Verify 'zpool clear' can clear errors of a storage pool."
	atf_set "require.progs"  zpool zfs
	atf_set "timeout" 2100
}
zpool_clear_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	ksh93 $(atf_get_srcdir)/zpool_clear_001_pos.ksh || atf_fail "Testcase failed"
}
zpool_clear_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_clear_002_neg cleanup
zpool_clear_002_neg_head()
{
	atf_set "descr" "Execute 'zpool clear' using invalid parameters."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2100
}
zpool_clear_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	ksh93 $(atf_get_srcdir)/zpool_clear_002_neg.ksh || atf_fail "Testcase failed"
}
zpool_clear_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_clear_003_neg cleanup
zpool_clear_003_neg_head()
{
	atf_set "descr" "Verify 'zpool clear' cannot clear error for available spare devices."
	atf_set "require.progs"  zpool
	atf_set "timeout" 2100
}
zpool_clear_003_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	ksh93 $(atf_get_srcdir)/zpool_clear_003_neg.ksh || atf_fail "Testcase failed"
}
zpool_clear_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zpool_clear_004_pos cleanup
zpool_clear_004_pos_head()
{
	atf_set "descr" "Verify 'zpool clear' can work on spare vdevs"
	atf_set "require.progs"  zpool
	atf_set "timeout" 2100
}
zpool_clear_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	ksh93 $(atf_get_srcdir)/zpool_clear_004_pos.ksh || atf_fail "Testcase failed"
}
zpool_clear_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zpool_clear_005_pos cleanup
zpool_clear_005_pos_head()
{
	atf_set "descr" "'zpool clear' can online an UNAVAIL pool after all vdevs have reappeared"
	atf_set "require.progs"  gnop zpool
}
zpool_clear_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	verify_disk_count "$DISKS" 3
	ksh93 $(atf_get_srcdir)/zpool_clear_005_pos.ksh || atf_fail "Testcase failed"
}
zpool_clear_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_clear.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}



atf_init_test_cases()
{

	atf_add_test_case zpool_clear_001_pos
	atf_add_test_case zpool_clear_002_neg
	atf_add_test_case zpool_clear_003_neg
	atf_add_test_case zpool_clear_004_pos
	atf_add_test_case zpool_clear_005_pos
}
