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


atf_test_case zfs_mount_001_pos cleanup
zfs_mount_001_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $mountcmd <filesystem>' succeeds as root."
	atf_set "require.progs"  zfs
}
zfs_mount_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	if other_pools_exist; then
		atf_skip "Can't test unmount -a with existing pools"
	fi

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_mount_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_002_pos cleanup
zfs_mount_002_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $mountcmd' with a filesystemwhose name is not in 'zfs list' will fail with return code 1."
	atf_set "require.progs"  zfs
}
zfs_mount_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_mount_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_003_pos cleanup
zfs_mount_003_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $mountcmd' with a filesystemwhose mountpoint property is 'legacy' or 'none'  \will fail with return code 1."
	atf_set "require.progs"  zfs
}
zfs_mount_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_mount_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_004_pos cleanup
zfs_mount_004_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $mountcmd <filesystem>'with a mounted filesystem will fail with return code 1."
	atf_set "require.progs"  zfs
}
zfs_mount_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_mount_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_005_pos cleanup
zfs_mount_005_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $mountcmd' with a filesystemwhose mountpoint is currently in use will fail with return code 1."
	atf_set "require.progs"  zfs
}
zfs_mount_005_pos_body()
{
	[[ `uname -s` = "FreeBSD" ]] && atf_skip "Unlike Illumos, FreeBSD allows the behavior the prohibition of which is tested by this testcase"
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_mount_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_006_pos cleanup
zfs_mount_006_pos_head()
{
	atf_set "descr" "Verify that '$ZFS $mountcmd <filesystem>'which mountpoint be the identical or the top of an existing one  \will fail with return code 1."
	atf_set "require.progs"  zfs
}
zfs_mount_006_pos_body()
{
	[[ `uname -s` = "FreeBSD" ]] && atf_skip "Unlike Illumos, FreeBSD allows the behavior the prohibition of which is tested by this testcase"
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_mount_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_007_pos cleanup
zfs_mount_007_pos_head()
{
	atf_set "descr" "Verify '-o' will set filesystem property temporarily,without affecting the property that is stored on disk."
	atf_set "require.progs"  zfs
}
zfs_mount_007_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	atf_expect_fail "PR 115361 zfs get setuid doesn't reflect setuid state as set by zfs mount"
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_007_pos.ksh || atf_fail "Testcase failed"
}
zfs_mount_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_008_pos cleanup
zfs_mount_008_pos_head()
{
	atf_set "descr" "Verify 'zfs mount -O' will override existing mount point."
	atf_set "require.progs"  zfs
}
zfs_mount_008_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_008_pos.ksh || atf_fail "Testcase failed"
}
zfs_mount_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_009_neg cleanup
zfs_mount_009_neg_head()
{
	atf_set "descr" "Badly-formed 'zfs $mountcmd' with inapplicable scenariosshould return an error."
	atf_set "require.progs"  zfs
}
zfs_mount_009_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	if other_pools_exist; then
                atf_skip "Can't test unmount -a with existing pools"
        fi

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_009_neg.ksh || atf_fail "Testcase failed"
}
zfs_mount_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_010_neg cleanup
zfs_mount_010_neg_head()
{
	atf_set "descr" "zfs mount fails with mounted filesystem or busy mountpoint"
	atf_set "require.progs"  zfs
}
zfs_mount_010_neg_body()
{
	[[ `uname -s` = "FreeBSD" ]] && atf_skip "Unlike Illumos, FreeBSD allows the behavior the prohibition of which is tested by this testcase"
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_010_neg.ksh || atf_fail "Testcase failed"
}
zfs_mount_010_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_011_neg cleanup
zfs_mount_011_neg_head()
{
	atf_set "descr" "zfs mount fails with bad parameters"
	atf_set "require.progs"  zfs
}
zfs_mount_011_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_011_neg.ksh || atf_fail "Testcase failed"
}
zfs_mount_011_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_all_001_pos cleanup
zfs_mount_all_001_pos_head()
{
	atf_set "descr" "Verify that 'zfs $mountall' succeeds as root,and all available ZFS filesystems are mounted."
	atf_set "require.progs"  zfs
}
zfs_mount_all_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	if other_pools_exist; then
                atf_skip "Can't test unmount -a with existing pools"
        fi

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_all_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_mount_all_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_mount.kshlib
	. $(atf_get_srcdir)/zfs_mount.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_mount_001_pos
	atf_add_test_case zfs_mount_002_pos
	atf_add_test_case zfs_mount_003_pos
	atf_add_test_case zfs_mount_004_pos
	atf_add_test_case zfs_mount_005_pos
	atf_add_test_case zfs_mount_006_pos
	atf_add_test_case zfs_mount_007_pos
	atf_add_test_case zfs_mount_008_pos
	atf_add_test_case zfs_mount_009_neg
	atf_add_test_case zfs_mount_010_neg
	atf_add_test_case zfs_mount_011_neg
	atf_add_test_case zfs_mount_all_001_pos
}
