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


atf_test_case poolversion_001_pos cleanup
poolversion_001_pos_head()
{
	atf_set "descr" "zpool set version can upgrade a pool"
	atf_set "require.progs"  zpool
}
poolversion_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/poolversion_001_pos.ksh || atf_fail "Testcase failed"
}
poolversion_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case poolversion_002_pos cleanup
poolversion_002_pos_head()
{
	atf_set "descr" "zpool set version can only increment pool version"
	atf_set "require.progs"  zpool
}
poolversion_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/poolversion_002_pos.ksh || atf_fail "Testcase failed"
}
poolversion_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case poolversion_001_pos
	atf_add_test_case poolversion_002_pos
}
