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


atf_test_case rootpool_001_pos cleanup
rootpool_001_pos_head()
{
	atf_set "descr" "rootpool's bootfs property must be equal to <rootfs>"
}
rootpool_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	if ! is_zfsroot ; then
		atf_skip "This test requires a ZFS root filesystem."
	fi

	ksh93 $(atf_get_srcdir)/setup.ksh
	ksh93 $(atf_get_srcdir)/rootpool_001_pos.ksh || atf_fail "Testcase failed"
}
rootpool_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rootpool_002_neg cleanup
rootpool_002_neg_head()
{
	atf_set "descr" "zpool/zfs destory <rootpool> should return error"
	atf_set "require.progs"  zfs zpool
}
rootpool_002_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	if ! is_zfsroot ; then
		atf_skip "This test requires a ZFS root filesystem."
	fi

	ksh93 $(atf_get_srcdir)/rootpool_002_neg.ksh || atf_fail "Testcase failed"
}
rootpool_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rootpool_007_neg cleanup
rootpool_007_neg_head()
{
	atf_set "descr" "the zfs rootfs's compression property can not set to gzip and gzip[1-9]"
	atf_set "require.progs"  zfs
}
rootpool_007_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	if ! is_zfsroot ; then
		atf_skip "This test requires a ZFS root filesystem."
	fi

	ksh93 $(atf_get_srcdir)/rootpool_007_neg.ksh || atf_fail "Testcase failed"
}
rootpool_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case rootpool_001_pos
	atf_add_test_case rootpool_002_neg
	atf_add_test_case rootpool_007_neg
}
