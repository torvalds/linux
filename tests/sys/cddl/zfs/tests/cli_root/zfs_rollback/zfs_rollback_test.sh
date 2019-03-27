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


atf_test_case zfs_rollback_001_pos cleanup
zfs_rollback_001_pos_head()
{
	atf_set "descr" "'zfs rollback -r|-rf|-R|-Rf' will recursively destroy anysnapshots more recent than the one specified."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1800
}
zfs_rollback_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rollback_common.kshlib
	. $(atf_get_srcdir)/zfs_rollback.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rollback_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_rollback_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rollback_common.kshlib
	. $(atf_get_srcdir)/zfs_rollback.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rollback_002_pos cleanup
zfs_rollback_002_pos_head()
{
	atf_set "descr" "'zfs rollback -f' will force unmount any filesystems."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1800
}
zfs_rollback_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rollback_common.kshlib
	. $(atf_get_srcdir)/zfs_rollback.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rollback_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_rollback_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rollback_common.kshlib
	. $(atf_get_srcdir)/zfs_rollback.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rollback_003_neg cleanup
zfs_rollback_003_neg_head()
{
	atf_set "descr" "Separately verify 'zfs rollback ''|-f|-r|-rf will fail indifferent conditions."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1800
}
zfs_rollback_003_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rollback_common.kshlib
	. $(atf_get_srcdir)/zfs_rollback.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rollback_003_neg.ksh || atf_fail "Testcase failed"
}
zfs_rollback_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rollback_common.kshlib
	. $(atf_get_srcdir)/zfs_rollback.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rollback_004_neg cleanup
zfs_rollback_004_neg_head()
{
	atf_set "descr" "'zfs rollback' should fail with bad options,too many arguments,non-snapshot datasets or missing datasets."
	atf_set "require.progs"  zfs
	atf_set "timeout" 1800
}
zfs_rollback_004_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rollback_common.kshlib
	. $(atf_get_srcdir)/zfs_rollback.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rollback_004_neg.ksh || atf_fail "Testcase failed"
}
zfs_rollback_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_rollback_common.kshlib
	. $(atf_get_srcdir)/zfs_rollback.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_rollback_001_pos
	atf_add_test_case zfs_rollback_002_pos
	atf_add_test_case zfs_rollback_003_neg
	atf_add_test_case zfs_rollback_004_neg
}
