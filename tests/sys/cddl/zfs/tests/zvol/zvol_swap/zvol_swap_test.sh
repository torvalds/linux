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


atf_test_case zvol_swap_001_pos cleanup
zvol_swap_001_pos_head()
{
	atf_set "descr" "Verify that a zvol can be used as a swap device"
	atf_set "require.progs"  swap swapadd
}
zvol_swap_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_swap_001_pos.ksh || atf_fail "Testcase failed"
}
zvol_swap_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_swap_002_pos cleanup
zvol_swap_002_pos_head()
{
	atf_set "descr" "Using a zvol as swap space, fill with files until ENOSPC returned."
	atf_set "require.progs"  swap swapadd
}
zvol_swap_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_swap_002_pos.ksh || atf_fail "Testcase failed"
}
zvol_swap_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_swap_003_pos cleanup
zvol_swap_003_pos_head()
{
	atf_set "descr" "Verify that a zvol device can be used as a swap devicethrough /etc/vfstab configuration."
	atf_set "require.progs"  swapadd swap
}
zvol_swap_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_swap_003_pos.ksh || atf_fail "Testcase failed"
}
zvol_swap_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_swap_004_pos cleanup
zvol_swap_004_pos_head()
{
	atf_set "descr" "The minimum volume size should be a multiple of 2 pagesize bytes."
	atf_set "require.progs"  zfs swap pagesize swapadd
}
zvol_swap_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_swap_004_pos.ksh || atf_fail "Testcase failed"
}
zvol_swap_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_swap_005_pos cleanup
zvol_swap_005_pos_head()
{
	atf_set "descr" "swaplow + swaplen must be less than or equal to the volume size."
	atf_set "require.progs"  swap pagesize swapadd
}
zvol_swap_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_swap_005_pos.ksh || atf_fail "Testcase failed"
}
zvol_swap_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zvol_swap_006_pos cleanup
zvol_swap_006_pos_head()
{
	atf_set "descr" "Verify volume can be add as several segments, but overlappingare not allowed."
	atf_set "require.progs"  swap pagesize swapadd
}
zvol_swap_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zvol_swap_006_pos.ksh || atf_fail "Testcase failed"
}
zvol_swap_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zvol_swap.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zvol_swap_001_pos
	atf_add_test_case zvol_swap_002_pos
	atf_add_test_case zvol_swap_003_pos
	atf_add_test_case zvol_swap_004_pos
	atf_add_test_case zvol_swap_005_pos
	atf_add_test_case zvol_swap_006_pos
}
