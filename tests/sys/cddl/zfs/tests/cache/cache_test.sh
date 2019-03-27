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


atf_test_case cache_001_pos cleanup
cache_001_pos_head()
{
	atf_set "descr" "Creating a pool with a cache device succeeds."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_001_pos.ksh || atf_fail "Testcase failed"
}
cache_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_002_pos cleanup
cache_002_pos_head()
{
	atf_set "descr" "Adding a cache device to normal pool works."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_002_pos.ksh || atf_fail "Testcase failed"
}
cache_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_003_pos cleanup
cache_003_pos_head()
{
	atf_set "descr" "Adding an extra cache device works."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_003_pos.ksh || atf_fail "Testcase failed"
}
cache_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_004_neg cleanup
cache_004_neg_head()
{
	atf_set "descr" "Attaching a cache device fails."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_004_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_004_neg.ksh || atf_fail "Testcase failed"
}
cache_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_005_neg cleanup
cache_005_neg_head()
{
	atf_set "descr" "Replacing a cache device fails."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_005_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_005_neg.ksh || atf_fail "Testcase failed"
}
cache_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_006_pos cleanup
cache_006_pos_head()
{
	atf_set "descr" "Exporting and importing pool with cache devices passes."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_006_pos.ksh || atf_fail "Testcase failed"
}
cache_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_007_neg cleanup
cache_007_neg_head()
{
	atf_set "descr" "A mirror/raidz/raidz2 cache is not supported."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_007_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_007_neg.ksh || atf_fail "Testcase failed"
}
cache_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_008_neg cleanup
cache_008_neg_head()
{
	atf_set "descr" "A raidz/raidz2 cache can not be added to existed pool."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_008_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_008_neg.ksh || atf_fail "Testcase failed"
}
cache_008_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_009_pos cleanup
cache_009_pos_head()
{
	atf_set "descr" "Offline and online a cache device succeed."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_009_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_009_pos.ksh || atf_fail "Testcase failed"
}
cache_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_010_neg cleanup
cache_010_neg_head()
{
	atf_set "descr" "Cache device can only be disk or slice."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 1200
}
cache_010_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_010_neg.ksh || atf_fail "Testcase failed"
}
cache_010_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case cache_011_pos cleanup
cache_011_pos_head()
{
	atf_set "descr" "Remove cache device from pool with spare device should succeed"
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
cache_011_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/cache_011_pos.ksh || atf_fail "Testcase failed"
}
cache_011_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/cache.kshlib
	. $(atf_get_srcdir)/cache.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case cache_001_pos
	atf_add_test_case cache_002_pos
	atf_add_test_case cache_003_pos
	atf_add_test_case cache_004_neg
	atf_add_test_case cache_005_neg
	atf_add_test_case cache_006_pos
	atf_add_test_case cache_007_neg
	atf_add_test_case cache_008_neg
	atf_add_test_case cache_009_pos
	atf_add_test_case cache_010_neg
	atf_add_test_case cache_011_pos
}
