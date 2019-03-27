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


atf_test_case quota_001_pos cleanup
quota_001_pos_head()
{
	atf_set "descr" "Verify that file size is limited by the file system quota"
	atf_set "require.progs"  zfs
}
quota_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/quota_001_pos.ksh || atf_fail "Testcase failed"
}
quota_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case quota_002_pos cleanup
quota_002_pos_head()
{
	atf_set "descr" "Verify that a file write cannot exceed the file system quota"
	atf_set "require.progs"  zfs
}
quota_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/quota_002_pos.ksh || atf_fail "Testcase failed"
}
quota_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case quota_003_pos cleanup
quota_003_pos_head()
{
	atf_set "descr" "Verify that file size is limited by the file system quota(dataset version)"
	atf_set "require.progs"  zfs
}
quota_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/quota_003_pos.ksh || atf_fail "Testcase failed"
}
quota_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case quota_004_pos cleanup
quota_004_pos_head()
{
	atf_set "descr" "Verify that a file write cannot exceed the file system quota(dataset version)"
	atf_set "require.progs"  zfs
}
quota_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/quota_004_pos.ksh || atf_fail "Testcase failed"
}
quota_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case quota_005_pos cleanup
quota_005_pos_head()
{
	atf_set "descr" "Verify that quota does not inherit its value from parent."
	atf_set "require.progs"  zfs
}
quota_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/quota_005_pos.ksh || atf_fail "Testcase failed"
}
quota_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case quota_006_neg cleanup
quota_006_neg_head()
{
	atf_set "descr" "Verify cannot set quota lower than the space currently in use"
	atf_set "require.progs"  zfs
}
quota_006_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/quota_006_neg.ksh || atf_fail "Testcase failed"
}
quota_006_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/quota.kshlib
	. $(atf_get_srcdir)/quota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case quota_001_pos
	atf_add_test_case quota_002_pos
	atf_add_test_case quota_003_pos
	atf_add_test_case quota_004_pos
	atf_add_test_case quota_005_pos
	atf_add_test_case quota_006_neg
}
