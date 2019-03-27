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


atf_test_case rsend_001_pos cleanup
rsend_001_pos_head()
{
	atf_set "descr" "zfs send -R send replication stream up to the named snap."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_001_pos.ksh || atf_fail "Testcase failed"
}
rsend_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_002_pos cleanup
rsend_002_pos_head()
{
	atf_set "descr" "zfs send -I sends all incrementals from fs@init to fs@final."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_002_pos.ksh || atf_fail "Testcase failed"
}
rsend_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_003_pos cleanup
rsend_003_pos_head()
{
	atf_set "descr" "zfs send -I send all incrementals from dataset@init to clone@snap"
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_003_pos.ksh || atf_fail "Testcase failed"
}
rsend_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_004_pos cleanup
rsend_004_pos_head()
{
	atf_set "descr" "zfs send -R -i send incremental from fs@init to fs@final."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_004_pos.ksh || atf_fail "Testcase failed"
}
rsend_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_005_pos cleanup
rsend_005_pos_head()
{
	atf_set "descr" "zfs send -R -I send all the incremental between @init with @final"
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_005_pos.ksh || atf_fail "Testcase failed"
}
rsend_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_006_pos cleanup
rsend_006_pos_head()
{
	atf_set "descr" "Rename snapshot name will not change the dependent order."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_006_pos.ksh || atf_fail "Testcase failed"
}
rsend_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_007_pos cleanup
rsend_007_pos_head()
{
	atf_set "descr" "Rename parent filesystem name will not change the dependent order."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_007_pos.ksh || atf_fail "Testcase failed"
}
rsend_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_008_pos cleanup
rsend_008_pos_head()
{
	atf_set "descr" "Changes made by 'zfs promote' can be properly received."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_008_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_008_pos.ksh || atf_fail "Testcase failed"
}
rsend_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_009_pos cleanup
rsend_009_pos_head()
{
	atf_set "descr" "Verify zfs receive can handle out of space correctly."
	atf_set "require.progs"  zpool zfs
	atf_set "timeout" 2700
}
rsend_009_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_009_pos.ksh || atf_fail "Testcase failed"
}
rsend_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_010_pos cleanup
rsend_010_pos_head()
{
	atf_set "descr" "ZFS can handle stream with multiple identical (same GUID) snapshots"
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_010_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_010_pos.ksh || atf_fail "Testcase failed"
}
rsend_010_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_011_pos cleanup
rsend_011_pos_head()
{
	atf_set "descr" "Verify changes made by 'zfs inherit' can be properly received."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_011_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_011_pos.ksh || atf_fail "Testcase failed"
}
rsend_011_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_012_pos cleanup
rsend_012_pos_head()
{
	atf_set "descr" "Verify zfs send -R will backup all the filesystem propertiescorrectly."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_012_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_012_pos.ksh || atf_fail "Testcase failed"
}
rsend_012_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case rsend_013_pos cleanup
rsend_013_pos_head()
{
	atf_set "descr" "zfs receive -dF will destroy all the dataset that not existon the sender side"
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2700
}
rsend_013_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/rsend_013_pos.ksh || atf_fail "Testcase failed"
}
rsend_013_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/rsend.kshlib
	. $(atf_get_srcdir)/rsend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case rsend_001_pos
	atf_add_test_case rsend_002_pos
	atf_add_test_case rsend_003_pos
	atf_add_test_case rsend_004_pos
	atf_add_test_case rsend_005_pos
	atf_add_test_case rsend_006_pos
	atf_add_test_case rsend_007_pos
	atf_add_test_case rsend_008_pos
	atf_add_test_case rsend_009_pos
	atf_add_test_case rsend_010_pos
	atf_add_test_case rsend_011_pos
	atf_add_test_case rsend_012_pos
	atf_add_test_case rsend_013_pos
}
