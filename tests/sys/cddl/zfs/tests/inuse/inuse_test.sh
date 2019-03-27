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


atf_test_case inuse_005_pos
inuse_005_pos_head()
{
	atf_set "descr" "Verify newfs over active pool fails."
	atf_set "require.progs"  newfs zpool
	atf_set "require.user" root
}
inuse_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/inuse.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/inuse_005_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case inuse_010_neg
inuse_010_neg_head()
{
	atf_set "descr" "ZFS shouldn't be able to use a disk with a mounted filesystem"
	atf_set "require.progs"  newfs zpool
	atf_set "require.user" root
}
inuse_010_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/inuse_010_neg.ksh || atf_fail "Testcase failed"
}


atf_init_test_cases()
{
	atf_add_test_case inuse_005_pos
	atf_add_test_case inuse_010_neg
}
