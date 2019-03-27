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


atf_test_case zinject_001_pos cleanup
zinject_001_pos_head()
{
	atf_set "descr" "Verify fault inject handle content error successfully."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
zinject_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zinject.kshlib
	. $(atf_get_srcdir)/zinject.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zinject_001_pos.ksh || atf_fail "Testcase failed"
}
zinject_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zinject.kshlib
	. $(atf_get_srcdir)/zinject.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zinject_002_pos cleanup
zinject_002_pos_head()
{
	atf_set "descr" "Verify fault inject handle metadnode error successfully."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
zinject_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zinject.kshlib
	. $(atf_get_srcdir)/zinject.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zinject_002_pos.ksh || atf_fail "Testcase failed"
}
zinject_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zinject.kshlib
	. $(atf_get_srcdir)/zinject.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zinject_003_pos cleanup
zinject_003_pos_head()
{
	atf_set "descr" "Verify fault inject handle into first metadnodecause filesystem unmountable."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
zinject_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zinject.kshlib
	. $(atf_get_srcdir)/zinject.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zinject_003_pos.ksh || atf_fail "Testcase failed"
}
zinject_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zinject.kshlib
	. $(atf_get_srcdir)/zinject.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zinject_004_pos cleanup
zinject_004_pos_head()
{
	atf_set "descr" "Verify fault inject handle device error successfully."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1800
}
zinject_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zinject.kshlib
	. $(atf_get_srcdir)/zinject.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zinject_004_pos.ksh || atf_fail "Testcase failed"
}
zinject_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zinject.kshlib
	. $(atf_get_srcdir)/zinject.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zinject_001_pos
	atf_add_test_case zinject_002_pos
	atf_add_test_case zinject_003_pos
	atf_add_test_case zinject_004_pos
}
