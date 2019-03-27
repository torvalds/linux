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


atf_test_case zfs_unmount_001_pos cleanup
zfs_unmount_001_pos_head()
{
	atf_set "descr" "Verify the u[n]mount [-f] sub-command."
	atf_set "require.progs"  zfs
}
zfs_unmount_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_unmount_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_002_pos cleanup
zfs_unmount_002_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $unmountcmd [-f] <filesystem|mountpoint>'whose name is not in 'zfs list' will fail with return code 1."
	atf_set "require.progs"  zfs
}
zfs_unmount_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_unmount_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_003_pos cleanup
zfs_unmount_003_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $unmountcmd [-f] <filesystem|mountpoint>'whose mountpoint property is 'legacy' or 'none'  \will fail with return code 1."
	atf_set "require.progs"  zfs
}
zfs_unmount_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_unmount_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_004_pos cleanup
zfs_unmount_004_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $unmountcmd [-f] <filesystem|mountpoint>'with an unmounted filesystem will fail with return code 1."
	atf_set "require.progs"  zfs
}
zfs_unmount_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_unmount_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_005_pos cleanup
zfs_unmount_005_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $unmountcmd <filesystem|mountpoint>'with a filesystem which mountpoint is currently in use  \will fail with return code 1, and forcefully will succeeds as root."
	atf_set "require.progs"  zfs
}
zfs_unmount_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_unmount_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_006_pos cleanup
zfs_unmount_006_pos_head()
{
	atf_set "descr" "Re-creating zfs files, 'zfs unmount' still succeed."
	atf_set "require.progs"  zfs
}
zfs_unmount_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_unmount_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_007_neg cleanup
zfs_unmount_007_neg_head()
{
	atf_set "descr" "Badly-formed 'zfs $unmountcmd' with inapplicable scenariosshould return an error."
	atf_set "require.progs"  zfs
}
zfs_unmount_007_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_007_neg.ksh || atf_fail "Testcase failed"
}
zfs_unmount_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_008_neg cleanup
zfs_unmount_008_neg_head()
{
	atf_set "descr" "zfs unmount fails with bad parameters or scenarios"
	atf_set "require.progs"  zfs
}
zfs_unmount_008_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_008_neg.ksh || atf_fail "Testcase failed"
}
zfs_unmount_008_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_009_pos cleanup
zfs_unmount_009_pos_head()
{
	atf_set "descr" "zfs fource unmount and destroy in snapshot directory will not cause error."
	atf_set "require.progs"  zfs zpool
}
zfs_unmount_009_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	if other_pools_exist; then
		atf_skip "Can't test unmount -a with existing pools"
	fi

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_009_pos.ksh || atf_fail "Testcase failed"
}
zfs_unmount_009_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_all_001_pos cleanup
zfs_unmount_all_001_pos_head()
{
	atf_set "descr" "Verify that 'zfs $unmountall' succeeds as root,and all available ZFS filesystems are unmounted."
	atf_set "require.progs"  zfs
}
zfs_unmount_all_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	if other_pools_exist; then
		atf_skip "Can't test unmount -a with existing pools"
	fi

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_all_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_unmount_all_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_unmount.kshlib
	. $(atf_get_srcdir)/zfs_unmount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_unmount_001_pos
	atf_add_test_case zfs_unmount_002_pos
	atf_add_test_case zfs_unmount_003_pos
	atf_add_test_case zfs_unmount_004_pos
	atf_add_test_case zfs_unmount_005_pos
	atf_add_test_case zfs_unmount_006_pos
	atf_add_test_case zfs_unmount_007_neg
	atf_add_test_case zfs_unmount_008_neg
	atf_add_test_case zfs_unmount_009_pos
	atf_add_test_case zfs_unmount_all_001_pos
}
