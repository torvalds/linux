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


atf_test_case pool_names_001_pos
pool_names_001_pos_head()
{
	atf_set "descr" "Ensure that pool names can use the ASCII subset of UTF-8"
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1200
}
pool_names_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/pool_names.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/pool_names_001_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case pool_names_002_neg
pool_names_002_neg_head()
{
	atf_set "descr" "Ensure that a set of invalid names cannot be used to create pools."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
pool_names_002_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/pool_names.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/pool_names_002_neg.ksh || atf_fail "Testcase failed"
}


atf_init_test_cases()
{

	atf_add_test_case pool_names_001_pos
	atf_add_test_case pool_names_002_neg
}
