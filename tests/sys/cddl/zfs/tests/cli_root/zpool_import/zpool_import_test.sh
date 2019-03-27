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


atf_test_case zpool_import_002_pos cleanup
zpool_import_002_pos_head()
{
	atf_set "descr" "Verify that an exported pool can be imported and cannot be imported more than once."
	atf_set "require.progs"  zfs zpool sum zdb
	atf_set "timeout" 2400
}
zpool_import_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_002_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_003_pos cleanup
zpool_import_003_pos_head()
{
	atf_set "descr" "Destroyed pools are not listed unless with -D option is specified."
	atf_set "require.progs"  zpool zfs
	atf_set "timeout" 2400
}
zpool_import_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_003_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_004_pos cleanup
zpool_import_004_pos_head()
{
	atf_set "descr" "Destroyed pools devices was moved to another directory,it still can be imported correctly."
	atf_set "require.progs"  zpool zfs zdb
	atf_set "timeout" 2400
}
zpool_import_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_004_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_005_pos cleanup
zpool_import_005_pos_head()
{
	atf_set "descr" "Destroyed pools devices was renamed, it still can be importedcorrectly."
	atf_set "require.progs"  zpool zfs zdb
	atf_set "timeout" 2400
}
zpool_import_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_005_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_006_pos cleanup
zpool_import_006_pos_head()
{
	atf_set "descr" "For mirror, N-1 destroyed pools devices was removed or usedby other pool, it still can be imported correctly."
	atf_set "require.progs"  zpool zfs zdb
	atf_set "timeout" 2400
}
zpool_import_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_006_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_007_pos cleanup
zpool_import_007_pos_head()
{
	atf_set "descr" "For raidz, one destroyed pools devices was removed or used byother pool, it still can be imported correctly."
	atf_set "require.progs"  zpool zfs zdb
	atf_set "timeout" 2400
}
zpool_import_007_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_007_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_008_pos cleanup
zpool_import_008_pos_head()
{
	atf_set "descr" "For raidz2, two destroyed pools devices was removed or used byother pool, it still can be imported correctly."
	atf_set "require.progs"  zpool zfs zdb
	atf_set "timeout" 2400
}
zpool_import_008_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_008_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_009_neg cleanup
zpool_import_009_neg_head()
{
	atf_set "descr" "Badly-formed 'zpool import' with inapplicable scenariosshould return an error."
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 2400
}
zpool_import_009_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_009_neg.ksh || atf_fail "Testcase failed"
}
zpool_import_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_010_pos cleanup
zpool_import_010_pos_head()
{
	atf_set "descr" "'zpool -D -a' can import all the specified directoriesdestroyed pools."
	atf_set "require.progs"  zpool zfs
	atf_set "timeout" 2400
}
zpool_import_010_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_010_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_010_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_011_neg cleanup
zpool_import_011_neg_head()
{
	atf_set "descr" "For strip pool, any destroyed pool devices was demaged,zpool import -D will failed."
	atf_set "require.progs"  zpool zfs zdb
	atf_set "timeout" 2400
}
zpool_import_011_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_011_neg.ksh || atf_fail "Testcase failed"
}
zpool_import_011_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_012_pos cleanup
zpool_import_012_pos_head()
{
	atf_set "descr" "Verify all mount & share status of sub-filesystems within a poolcan be restored after import [-Df]."
	atf_set "require.progs"  zfs zpool zdb share
	atf_set "timeout" 2400
}
zpool_import_012_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_012_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_012_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_013_neg
zpool_import_013_neg_head()
{
	atf_set "descr" "'zpool import' fails for pool that was not cleanly exported"
	atf_set "require.progs"  zfs zpool
}
zpool_import_013_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/zpool_import_013_neg.ksh || atf_fail "Testcase failed"
}


atf_test_case zpool_import_014_pos cleanup
zpool_import_014_pos_head()
{
	atf_set "descr" "'zpool import' can import destroyed disk-backed pools"
	atf_set "require.progs"  zfs zpool
}
zpool_import_014_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/zpool_import_014_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_014_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_all_001_pos cleanup
zpool_import_all_001_pos_head()
{
	atf_set "descr" "Verify that 'zpool import -a' succeeds as root."
	atf_set "require.progs"  zfs zpool sum
	atf_set "timeout" 2400
}
zpool_import_all_001_pos_body()
{
	atf_skip "This test relies heavily on Solaris slices.  It could be ported, but that is difficult due to the high degree of obfuscation in the code"
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_all_001_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_all_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_missing_001_pos cleanup
zpool_import_missing_001_pos_head()
{
	atf_set "descr" "Verify that import could handle damaged or missing device."
	atf_set "require.progs"  zfs sum zpool zdb
	atf_set "timeout" 2400
}
zpool_import_missing_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_missing_001_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_missing_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_missing_002_pos cleanup
zpool_import_missing_002_pos_head()
{
	atf_set "descr" "Verify that import could handle moving device."
	atf_set "require.progs"  zpool zfs zdb
	atf_set "timeout" 2400
}
zpool_import_missing_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_missing_002_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_missing_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_missing_003_pos cleanup
zpool_import_missing_003_pos_head()
{
	atf_set "descr" "Verify that import could handle device overlapped."
	atf_set "require.progs"  zpool sum zfs
	atf_set "timeout" 2400
}
zpool_import_missing_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_missing_003_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_missing_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zpool_import_missing_004_pos
zpool_import_missing_004_pos_head()
{
	atf_set "descr" "Verify that zpool import succeeds when devices are missing"
	atf_set "require.progs"  zfs zpool
	atf_set "timeout" 300
}
zpool_import_missing_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/zpool_import_missing_004_pos.ksh || atf_fail "Testcase failed"
}

atf_test_case zpool_import_missing_005_pos
zpool_import_missing_005_pos_head()
{
	atf_set "descr" "Verify that zpool import succeeds when devices of all types have been renamed"
	atf_set "require.progs"  mdconfig zfs zpool
	atf_set "timeout" 300
}
zpool_import_missing_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/zpool_import_missing_005_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case zpool_import_rename_001_pos cleanup
zpool_import_rename_001_pos_head()
{
	atf_set "descr" "Verify that an imported pool can be renamed."
	atf_set "require.progs"  zfs zpool sum zdb
	atf_set "timeout" 2400
}
zpool_import_rename_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_rename_001_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_rename_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zpool_import_corrupt_001_pos cleanup
zpool_import_corrupt_001_pos_head()
{
	atf_set "descr" "Verify that a disk-backed exported pool with some of its vdev labels corrupted can still be imported"
	atf_set "require.progs"  zfs zpool zdb
	atf_set "timeout" 2400
}
zpool_import_corrupt_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/zpool_import_corrupt_001_pos.ksh || atf_fail "Testcase failed"
}
zpool_import_corrupt_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_import.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zpool_import_destroyed_001_neg cleanup
zpool_import_destroyed_001_neg_head()
{
	atf_set "descr" "'zpool import' will not show destroyed pools, even if an out-of-date non-destroyed label remains"
	atf_set "require.progs"  zpool
}
zpool_import_destroyed_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg

	verify_disk_count "$DISKS" 3
	ksh93 $(atf_get_srcdir)/zpool_import_destroyed_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_import_destroyed_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg

	destroy_pool "$TESTPOOL"
	cleanup_devices "$DISKS"
}

atf_test_case zpool_import_destroyed_002_neg cleanup
zpool_import_destroyed_002_neg_head()
{
	atf_set "descr" "'zpool import' will not show destroyed pools, even if an out-of-date non-destroyed label remains"
	atf_set "require.progs"  zpool
}
zpool_import_destroyed_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/zpool_import_destroyed_002_neg.ksh || atf_fail "Testcase failed"
}
zpool_import_destroyed_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg

	destroy_pool "$TESTPOOL"
	cleanup_devices "$DISKS"
}


atf_init_test_cases()
{

	atf_add_test_case zpool_import_002_pos
	atf_add_test_case zpool_import_003_pos
	atf_add_test_case zpool_import_004_pos
	atf_add_test_case zpool_import_005_pos
	atf_add_test_case zpool_import_006_pos
	atf_add_test_case zpool_import_007_pos
	atf_add_test_case zpool_import_008_pos
	atf_add_test_case zpool_import_009_neg
	atf_add_test_case zpool_import_010_pos
	atf_add_test_case zpool_import_011_neg
	atf_add_test_case zpool_import_012_pos
	atf_add_test_case zpool_import_013_neg
	atf_add_test_case zpool_import_014_pos
	atf_add_test_case zpool_import_all_001_pos
	atf_add_test_case zpool_import_missing_001_pos
	atf_add_test_case zpool_import_missing_002_pos
	atf_add_test_case zpool_import_missing_003_pos
	atf_add_test_case zpool_import_missing_004_pos
	atf_add_test_case zpool_import_missing_005_pos
	atf_add_test_case zpool_import_rename_001_pos
	atf_add_test_case zpool_import_corrupt_001_pos
	atf_add_test_case zpool_import_destroyed_001_neg
	atf_add_test_case zpool_import_destroyed_002_neg
}
