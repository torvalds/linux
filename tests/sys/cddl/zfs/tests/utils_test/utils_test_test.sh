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


atf_test_case utils_test_001_pos cleanup
utils_test_001_pos_head()
{
	atf_set "descr" "Ensure that the clri(1M) utility fails on a ZFS file system."
	atf_set "require.progs"  clri
}
utils_test_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/utils_test_001_pos.ksh || atf_fail "Testcase failed"
}
utils_test_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case utils_test_002_pos cleanup
utils_test_002_pos_head()
{
	atf_set "descr" "Ensure that the labelit(1M) utility fails on a ZFS file system."
	atf_set "require.progs"  zfs labelit
}
utils_test_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/utils_test_002_pos.ksh || atf_fail "Testcase failed"
}
utils_test_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case utils_test_003_pos cleanup
utils_test_003_pos_head()
{
	atf_set "descr" "Ensure that the fsdb(1M) utility fails on a ZFS file system."
	atf_set "require.progs"  fsdb
}
utils_test_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/utils_test_003_pos.ksh || atf_fail "Testcase failed"
}
utils_test_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case utils_test_004_pos cleanup
utils_test_004_pos_head()
{
	atf_set "descr" "Ensure that the quotaon(1M) utility fails on a ZFS file system."
	atf_set "require.progs"  zfs quotaon
}
utils_test_004_pos_body()
{
	atf_expect_fail "FreeBSD's quotaon utility exits 0 even when you supply a nonexistent filesystem"
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/utils_test_004_pos.ksh || atf_fail "Testcase failed"
}
utils_test_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case utils_test_005_pos cleanup
utils_test_005_pos_head()
{
	atf_set "descr" "Ensure that the ff(1M) utility fails on a ZFS file system."
	atf_set "require.progs"  ff
}
utils_test_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/utils_test_005_pos.ksh || atf_fail "Testcase failed"
}
utils_test_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case utils_test_006_pos cleanup
utils_test_006_pos_head()
{
	atf_set "descr" "Ensure that the fsirand(1M) utility fails on a ZFS file system."
	atf_set "require.progs"  zfs fsirand
}
utils_test_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/utils_test_006_pos.ksh || atf_fail "Testcase failed"
}
utils_test_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case utils_test_007_pos cleanup
utils_test_007_pos_head()
{
	atf_set "descr" "Ensure that the fstyp(1M) utility succeeds on a ZFS file system."
	atf_set "require.progs"  zfs fstyp
}
utils_test_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/utils_test_007_pos.ksh || atf_fail "Testcase failed"
}
utils_test_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case utils_test_008_pos cleanup
utils_test_008_pos_head()
{
	atf_set "descr" "Ensure that the ncheck(1M) utility fails on a ZFS file system."
	atf_set "require.progs"  zfs ncheck
}
utils_test_008_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/utils_test_008_pos.ksh || atf_fail "Testcase failed"
}
utils_test_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case utils_test_009_pos cleanup
utils_test_009_pos_head()
{
	atf_set "descr" "Ensure that the tunefs(1M) utility fails on a ZFS file system."
	atf_set "require.progs"  tunefs
}
utils_test_009_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/utils_test_009_pos.ksh || atf_fail "Testcase failed"
}
utils_test_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/utils_test.kshlib
	. $(atf_get_srcdir)/utils_test.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case utils_test_001_pos
	atf_add_test_case utils_test_002_pos
	atf_add_test_case utils_test_003_pos
	atf_add_test_case utils_test_004_pos
	atf_add_test_case utils_test_005_pos
	atf_add_test_case utils_test_006_pos
	atf_add_test_case utils_test_007_pos
	atf_add_test_case utils_test_008_pos
	atf_add_test_case utils_test_009_pos
}
