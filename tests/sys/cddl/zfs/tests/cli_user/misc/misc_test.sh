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


atf_test_case zdb_001_neg cleanup
zdb_001_neg_head()
{
	atf_set "descr" "zdb can't run as a user on datasets, but can run without arguments"
	atf_set "require.progs"  zfs fgrep zpool zdb
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zdb_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zdb_001_neg.ksh || atf_fail "Testcase failed"
}
zdb_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_001_neg cleanup
zfs_001_neg_head()
{
	atf_set "descr" "zfs shows a usage message when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_001_neg cleanup
zfs_allow_001_neg_head()
{
	atf_set "descr" "zfs allow returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep logname zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_allow_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_allow_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_clone_001_neg cleanup
zfs_clone_001_neg_head()
{
	atf_set "descr" "zfs clone returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_clone_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_clone_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_clone_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_create_001_neg cleanup
zfs_create_001_neg_head()
{
	atf_set "descr" "Verify zfs create without parameters fails."
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_create_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_create_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_create_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_destroy_001_neg cleanup
zfs_destroy_001_neg_head()
{
	atf_set "descr" "zfs destroy [-f|-r] [fs|snap]"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_destroy_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_destroy_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_destroy_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_get_001_neg cleanup
zfs_get_001_neg_head()
{
	atf_set "descr" "zfs get works when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_get_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_get_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_get_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_inherit_001_neg cleanup
zfs_inherit_001_neg_head()
{
	atf_set "descr" "zfs inherit returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_inherit_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_inherit_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_inherit_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_mount_001_neg cleanup
zfs_mount_001_neg_head()
{
	atf_set "descr" "zfs mount returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_mount_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_mount_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_mount_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_promote_001_neg cleanup
zfs_promote_001_neg_head()
{
	atf_set "descr" "zfs promote returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_promote_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_promote_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_promote_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_receive_001_neg cleanup
zfs_receive_001_neg_head()
{
	atf_set "descr" "zfs receive returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_receive_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_receive_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_receive_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rename_001_neg cleanup
zfs_rename_001_neg_head()
{
	atf_set "descr" "zfs rename returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_rename_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rename_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_rename_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_rollback_001_neg cleanup
zfs_rollback_001_neg_head()
{
	atf_set "descr" "zfs rollback returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_rollback_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_rollback_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_rollback_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_send_001_neg cleanup
zfs_send_001_neg_head()
{
	atf_set "descr" "zfs send returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_send_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_send_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_send_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_set_001_neg cleanup
zfs_set_001_neg_head()
{
	atf_set "descr" "zfs set returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_set_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_set_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_set_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_share_001_neg cleanup
zfs_share_001_neg_head()
{
	atf_set "descr" "zfs share returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_share_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_share_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_share_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_snapshot_001_neg cleanup
zfs_snapshot_001_neg_head()
{
	atf_set "descr" "zfs snapshot returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_snapshot_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_snapshot_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_snapshot_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unallow_001_neg cleanup
zfs_unallow_001_neg_head()
{
	atf_set "descr" "zfs unallow returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_unallow_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unallow_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_unallow_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unmount_001_neg cleanup
zfs_unmount_001_neg_head()
{
	atf_set "descr" "zfs u[n]mount [-f] [mountpoint|fs|snap]"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_unmount_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unmount_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_unmount_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_unshare_001_neg cleanup
zfs_unshare_001_neg_head()
{
	atf_set "descr" "zfs unshare returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep share zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_unshare_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_unshare_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_unshare_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_upgrade_001_neg cleanup
zfs_upgrade_001_neg_head()
{
	atf_set "descr" "zfs upgrade returns an error when run as a user"
	atf_set "require.progs"  zfs fgrep zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zfs_upgrade_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_upgrade_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_upgrade_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_001_neg cleanup
zpool_001_neg_head()
{
	atf_set "descr" "zpool shows a usage message when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_add_001_neg cleanup
zpool_add_001_neg_head()
{
	atf_set "descr" "zpool add [-fn] pool_name vdev"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_add_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_add_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_add_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_add_002_pos cleanup
zpool_add_002_pos_head()
{
	atf_set "descr" "zpool add [-f] -n succeeds for unpriveleged users"
	atf_set "require.progs"  zfs zpool
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_add_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_add_002_pos.ksh || atf_fail "Testcase failed"
}
zpool_add_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_attach_001_neg cleanup
zpool_attach_001_neg_head()
{
	atf_set "descr" "zpool attach returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_attach_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_attach_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_attach_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_clear_001_neg cleanup
zpool_clear_001_neg_head()
{
	atf_set "descr" "zpool clear returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_clear_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_clear_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_clear_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_create_001_neg
zpool_create_001_neg_head()
{
	atf_set "descr" "zpool create [-f] fails for unpriveleged users"
	atf_set "require.progs"  zfs zpool
	atf_set "require.user" unprivileged
}
zpool_create_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/zpool_create_001_neg.ksh || atf_fail "Testcase failed"
}


atf_test_case zpool_create_002_pos
zpool_create_002_pos_head()
{
	atf_set "descr" "zpool create [-f] -n succeeds for unpriveleged users"
	atf_set "require.progs"  zfs zpool
	atf_set "require.user" unprivileged
}
zpool_create_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/zpool_create_002_pos.ksh || atf_fail "Testcase failed"
}


atf_test_case zpool_destroy_001_neg cleanup
zpool_destroy_001_neg_head()
{
	atf_set "descr" "zpool destroy [-f] [pool_name ...]"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_destroy_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_destroy_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_destroy_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_detach_001_neg cleanup
zpool_detach_001_neg_head()
{
	atf_set "descr" "zpool detach returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_detach_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_detach_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_detach_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_export_001_neg cleanup
zpool_export_001_neg_head()
{
	atf_set "descr" "zpool export returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_export_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_export_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_export_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_get_001_neg cleanup
zpool_get_001_neg_head()
{
	atf_set "descr" "zpool get works when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_get_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_get_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_get_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_history_001_neg cleanup
zpool_history_001_neg_head()
{
	atf_set "descr" "zpool history returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_history_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_history_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_history_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_001_neg cleanup
zpool_import_001_neg_head()
{
	atf_set "descr" "zpool import returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_import_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_import_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_import_002_neg cleanup
zpool_import_002_neg_head()
{
	atf_set "descr" "Executing 'zpool import' by regular user fails"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_import_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_import_002_neg.ksh || atf_fail "Testcase failed"
}
zpool_import_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_offline_001_neg cleanup
zpool_offline_001_neg_head()
{
	atf_set "descr" "zpool offline returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_offline_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_offline_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_offline_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_online_001_neg cleanup
zpool_online_001_neg_head()
{
	atf_set "descr" "zpool online returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_online_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_online_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_online_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_remove_001_neg cleanup
zpool_remove_001_neg_head()
{
	atf_set "descr" "zpool remove returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_remove_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_remove_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_remove_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_replace_001_neg cleanup
zpool_replace_001_neg_head()
{
	atf_set "descr" "zpool replace returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_replace_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_replace_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_replace_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_scrub_001_neg cleanup
zpool_scrub_001_neg_head()
{
	atf_set "descr" "zpool scrub returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_scrub_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_scrub_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_scrub_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_set_001_neg cleanup
zpool_set_001_neg_head()
{
	atf_set "descr" "zpool set returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_set_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_set_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_set_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_status_001_neg cleanup
zpool_status_001_neg_head()
{
	atf_set "descr" "zpool status works when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_status_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_status_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_status_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_upgrade_001_neg cleanup
zpool_upgrade_001_neg_head()
{
	atf_set "descr" "zpool upgrade returns an error when run as a user"
	atf_set "require.progs"  zfs zpool fgrep
	atf_set "require.user" root
	atf_set "require.config" unprivileged_user
}
zpool_upgrade_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/misc.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zdb_001_neg
	atf_add_test_case zfs_001_neg
	atf_add_test_case zfs_allow_001_neg
	atf_add_test_case zfs_clone_001_neg
	atf_add_test_case zfs_create_001_neg
	atf_add_test_case zfs_destroy_001_neg
	atf_add_test_case zfs_get_001_neg
	atf_add_test_case zfs_inherit_001_neg
	atf_add_test_case zfs_mount_001_neg
	atf_add_test_case zfs_promote_001_neg
	atf_add_test_case zfs_receive_001_neg
	atf_add_test_case zfs_rename_001_neg
	atf_add_test_case zfs_rollback_001_neg
	atf_add_test_case zfs_send_001_neg
	atf_add_test_case zfs_set_001_neg
	atf_add_test_case zfs_share_001_neg
	atf_add_test_case zfs_snapshot_001_neg
	atf_add_test_case zfs_unallow_001_neg
	atf_add_test_case zfs_unmount_001_neg
	atf_add_test_case zfs_unshare_001_neg
	atf_add_test_case zfs_upgrade_001_neg
	atf_add_test_case zpool_001_neg
	atf_add_test_case zpool_add_001_neg
	atf_add_test_case zpool_add_002_pos
	atf_add_test_case zpool_attach_001_neg
	atf_add_test_case zpool_clear_001_neg
	atf_add_test_case zpool_create_001_neg
	atf_add_test_case zpool_create_002_pos
	atf_add_test_case zpool_destroy_001_neg
	atf_add_test_case zpool_detach_001_neg
	atf_add_test_case zpool_export_001_neg
	atf_add_test_case zpool_get_001_neg
	atf_add_test_case zpool_history_001_neg
	atf_add_test_case zpool_import_001_neg
	atf_add_test_case zpool_import_002_neg
	atf_add_test_case zpool_offline_001_neg
	atf_add_test_case zpool_online_001_neg
	atf_add_test_case zpool_remove_001_neg
	atf_add_test_case zpool_replace_001_neg
	atf_add_test_case zpool_scrub_001_neg
	atf_add_test_case zpool_set_001_neg
	atf_add_test_case zpool_status_001_neg
	atf_add_test_case zpool_upgrade_001_neg
}
