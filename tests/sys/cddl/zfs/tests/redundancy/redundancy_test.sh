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


atf_test_case redundancy_001_pos cleanup
redundancy_001_pos_head()
{
	atf_set "descr" "Verify raidz pool can withstand one device is failing."
	atf_set "timeout" 1800
}
redundancy_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/redundancy.kshlib
	. $(atf_get_srcdir)/redundancy.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/redundancy_001_pos.ksh || atf_fail "Testcase failed"
}
redundancy_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/redundancy.kshlib
	. $(atf_get_srcdir)/redundancy.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case redundancy_002_pos cleanup
redundancy_002_pos_head()
{
	atf_set "descr" "Verify raidz2 pool can withstand two devices are failing."
	atf_set "timeout" 1800
}
redundancy_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/redundancy.kshlib
	. $(atf_get_srcdir)/redundancy.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/redundancy_002_pos.ksh || atf_fail "Testcase failed"
}
redundancy_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/redundancy.kshlib
	. $(atf_get_srcdir)/redundancy.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case redundancy_003_pos cleanup
redundancy_003_pos_head()
{
	atf_set "descr" "Verify mirrored pool can withstand N-1 devices are failing or missing."
	atf_set "timeout" 1800
}
redundancy_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/redundancy.kshlib
	. $(atf_get_srcdir)/redundancy.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/redundancy_003_pos.ksh || atf_fail "Testcase failed"
}
redundancy_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/redundancy.kshlib
	. $(atf_get_srcdir)/redundancy.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case redundancy_004_neg cleanup
redundancy_004_neg_head()
{
	atf_set "descr" "Verify striped pool have no data redundancy."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
redundancy_004_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/redundancy.kshlib
	. $(atf_get_srcdir)/redundancy.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/redundancy_004_neg.ksh || atf_fail "Testcase failed"
}
redundancy_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/redundancy.kshlib
	. $(atf_get_srcdir)/redundancy.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case redundancy_001_pos
	atf_add_test_case redundancy_002_pos
	atf_add_test_case redundancy_003_pos
	atf_add_test_case redundancy_004_neg
}
