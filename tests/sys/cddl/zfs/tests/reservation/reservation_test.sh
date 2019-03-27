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


atf_test_case reservation_001_pos cleanup
reservation_001_pos_head()
{
	atf_set "descr" "Verify that to set a reservation on a filesystem or volume must use value smaller than space \ available property of pool"
	atf_set "require.progs"  zfs
}
reservation_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_001_pos.ksh || atf_fail "Testcase failed"
}
reservation_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_002_pos cleanup
reservation_002_pos_head()
{
	atf_set "descr" "Reservation values cannot exceed the amount of space available in the pool"
	atf_set "require.progs"  zfs
}
reservation_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_002_pos.ksh || atf_fail "Testcase failed"
}
reservation_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_003_pos cleanup
reservation_003_pos_head()
{
	atf_set "descr" "Verify it is possible to set reservations multiple times on a filesystem regular and sparse volume"
	atf_set "require.progs"  zfs
}
reservation_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_003_pos.ksh || atf_fail "Testcase failed"
}
reservation_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_004_pos cleanup
reservation_004_pos_head()
{
	atf_set "descr" "Verify space released when a dataset with reservation is destroyed"
	atf_set "require.progs"  zfs
}
reservation_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_004_pos.ksh || atf_fail "Testcase failed"
}
reservation_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_005_pos cleanup
reservation_005_pos_head()
{
	atf_set "descr" "Verify space released when reservation on a dataset is setto 'none'"
	atf_set "require.progs"  zfs
}
reservation_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_005_pos.ksh || atf_fail "Testcase failed"
}
reservation_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_006_pos cleanup
reservation_006_pos_head()
{
	atf_set "descr" "Verify can create files both inside and outside reserved areas"
	atf_set "require.progs"  zfs
}
reservation_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_006_pos.ksh || atf_fail "Testcase failed"
}
reservation_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_007_pos cleanup
reservation_007_pos_head()
{
	atf_set "descr" "Verify reservations on data sets doesn't affect other data sets at same level except for consuming space from common pool"
	atf_set "require.progs"  zfs
}
reservation_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_007_pos.ksh || atf_fail "Testcase failed"
}
reservation_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_008_pos cleanup
reservation_008_pos_head()
{
	atf_set "descr" "Verify reducing reservation allows other datasets to use space"
	atf_set "require.progs"  zfs
	atf_set "timeout" 600
}
reservation_008_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_008_pos.ksh || atf_fail "Testcase failed"
}
reservation_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_009_pos cleanup
reservation_009_pos_head()
{
	atf_set "descr" "Setting top level dataset reservation to 'none' allows more data to be written to top level filesystem"
	atf_set "require.progs"  zfs
	atf_set "timeout" 600
}
reservation_009_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_009_pos.ksh || atf_fail "Testcase failed"
}
reservation_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_010_pos cleanup
reservation_010_pos_head()
{
	atf_set "descr" "Destroying top level filesystem with reservation allows more data to be written to another top level filesystem"
	atf_set "require.progs"  zfs
	atf_set "timeout" 600
}
reservation_010_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_010_pos.ksh || atf_fail "Testcase failed"
}
reservation_010_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_011_pos cleanup
reservation_011_pos_head()
{
	atf_set "descr" "Verify reservation settings do not affect quota settings"
	atf_set "require.progs"  zfs
}
reservation_011_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_011_pos.ksh || atf_fail "Testcase failed"
}
reservation_011_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_012_pos cleanup
reservation_012_pos_head()
{
	atf_set "descr" "Verify reservations protect space"
	atf_set "require.progs"  zfs
	atf_set "timeout" 600
}
reservation_012_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_012_pos.ksh || atf_fail "Testcase failed"
}
reservation_012_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_013_pos cleanup
reservation_013_pos_head()
{
	atf_set "descr" "Reservation properties preserved across exports and imports"
	atf_set "require.progs"  zfs zpool
}
reservation_013_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_013_pos.ksh || atf_fail "Testcase failed"
}
reservation_013_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_014_pos cleanup
reservation_014_pos_head()
{
	atf_set "descr" "Verify cannot set reservation larger than quota"
	atf_set "require.progs"  zfs
}
reservation_014_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_014_pos.ksh || atf_fail "Testcase failed"
}
reservation_014_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_015_pos cleanup
reservation_015_pos_head()
{
	atf_set "descr" "Setting volume reservation to 'none' allows more data to be written to top level filesystem"
	atf_set "require.progs"  zfs
	atf_set "timeout" 600
}
reservation_015_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_015_pos.ksh || atf_fail "Testcase failed"
}
reservation_015_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_016_pos cleanup
reservation_016_pos_head()
{
	atf_set "descr" "Destroying a regular volume with reservation allows more data to be written to top level filesystem"
	atf_set "require.progs"  zfs
	atf_set "timeout" 600
}
reservation_016_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_016_pos.ksh || atf_fail "Testcase failed"
}
reservation_016_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_017_pos cleanup
reservation_017_pos_head()
{
	atf_set "descr" "Verify that the volsize changes of sparse volume are not reflectedin the reservation"
	atf_set "require.progs"  zfs
}
reservation_017_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_017_pos.ksh || atf_fail "Testcase failed"
}
reservation_017_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case reservation_018_pos cleanup
reservation_018_pos_head()
{
	atf_set "descr" "Verify that reservation doesnot inherit its value from parent."
	atf_set "require.progs"  zfs
}
reservation_018_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/reservation_018_pos.ksh || atf_fail "Testcase failed"
}
reservation_018_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/reservation.kshlib
	. $(atf_get_srcdir)/reservation.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case reservation_001_pos
	atf_add_test_case reservation_002_pos
	atf_add_test_case reservation_003_pos
	atf_add_test_case reservation_004_pos
	atf_add_test_case reservation_005_pos
	atf_add_test_case reservation_006_pos
	atf_add_test_case reservation_007_pos
	atf_add_test_case reservation_008_pos
	atf_add_test_case reservation_009_pos
	atf_add_test_case reservation_010_pos
	atf_add_test_case reservation_011_pos
	atf_add_test_case reservation_012_pos
	atf_add_test_case reservation_013_pos
	atf_add_test_case reservation_014_pos
	atf_add_test_case reservation_015_pos
	atf_add_test_case reservation_016_pos
	atf_add_test_case reservation_017_pos
	atf_add_test_case reservation_018_pos
}
