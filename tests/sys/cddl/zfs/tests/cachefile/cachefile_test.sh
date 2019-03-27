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


atf_test_case cachefile_001_pos
cachefile_001_pos_head()
{
	atf_set "descr" "Creating a pool with \cachefile\ set doesn't update zpool.cache"
	atf_set "require.progs"  zpool
}
cachefile_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cachefile.kshlib
	. $(atf_get_srcdir)/cachefile.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/cachefile_001_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case cachefile_002_pos
cachefile_002_pos_head()
{
	atf_set "descr" "Importing a pool with \cachefile\ set doesn't update zpool.cache"
	atf_set "require.progs"  zpool
}
cachefile_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cachefile.kshlib
	. $(atf_get_srcdir)/cachefile.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/cachefile_002_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case cachefile_003_pos
cachefile_003_pos_head()
{
	atf_set "descr" "Setting altroot=path and cachefile=$CPATH for zpool create succeed."
	atf_set "require.progs"  zpool
}
cachefile_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cachefile.kshlib
	. $(atf_get_srcdir)/cachefile.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/cachefile_003_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case cachefile_004_pos
cachefile_004_pos_head()
{
	atf_set "descr" "Verify set, export and destroy when cachefile is set on pool."
	atf_set "require.progs"  zpool
}
cachefile_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cachefile.kshlib
	. $(atf_get_srcdir)/cachefile.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/cachefile_004_pos.ksh || atf_fail "Testcase failed"
}


atf_init_test_cases()
{

	atf_add_test_case cachefile_001_pos
	atf_add_test_case cachefile_002_pos
	atf_add_test_case cachefile_003_pos
	atf_add_test_case cachefile_004_pos
}
