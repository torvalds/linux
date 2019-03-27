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


atf_test_case zones_001_pos cleanup
zones_001_pos_head()
{
	atf_set "descr" "Local zone contains ZFS datasets as expected."
	atf_set "require.progs"  zfs zoneadm zonecfg
	atf_set "timeout" 3600
}
zones_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zones_001_pos.ksh || atf_fail "Testcase failed"
}
zones_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zones_002_pos cleanup
zones_002_pos_head()
{
	atf_set "descr" "A ZFS fs is created when the parent dir of zonepath is a ZFS fs."
	atf_set "require.progs"  zfs zoneadm zonecfg
	atf_set "timeout" 3600
}
zones_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zones_002_pos.ksh || atf_fail "Testcase failed"
}
zones_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zones_003_pos cleanup
zones_003_pos_head()
{
	atf_set "descr" "Zone cloning via ZFS snapshots works as expected."
	atf_set "require.progs"  zfs zoneadm zonecfg
	atf_set "timeout" 3600
}
zones_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zones_003_pos.ksh || atf_fail "Testcase failed"
}
zones_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zones_004_pos cleanup
zones_004_pos_head()
{
	atf_set "descr" "A ZFS fs is destroyed when the zone it was created for is deleted."
	atf_set "require.progs"  zfs zoneadm zonecfg
	atf_set "timeout" 3600
}
zones_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zones_004_pos.ksh || atf_fail "Testcase failed"
}
zones_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zones_005_pos cleanup
zones_005_pos_head()
{
	atf_set "descr" "Pool properties can be read but can't be set within a zone"
	atf_set "require.progs"  zpool zonecfg zoneadm
	atf_set "timeout" 3600
}
zones_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zones_005_pos.ksh || atf_fail "Testcase failed"
}
zones_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zones_common.kshlib
	. $(atf_get_srcdir)/zones.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zones_001_pos
	atf_add_test_case zones_002_pos
	atf_add_test_case zones_003_pos
	atf_add_test_case zones_004_pos
	atf_add_test_case zones_005_pos
}
