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


atf_test_case zfs_acl_chmod_001_neg cleanup
zfs_acl_chmod_001_neg_head()
{
	atf_set "descr" "Verify illegal operating to ACL, it will fail."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_001_neg.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_002_pos cleanup
zfs_acl_chmod_002_pos_head()
{
	atf_set "descr" "Verify acl after upgrading."
	atf_set "require.config" zfs_acl
	atf_set "require.progs"  zfs
}
zfs_acl_chmod_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_aclmode_001_pos cleanup
zfs_acl_chmod_aclmode_001_pos_head()
{
	atf_set "descr" "Verify chmod have correct behaviour to directory and file whenfilesystem has the different aclmode setting."
	atf_set "require.config" zfs_acl
	atf_set "require.progs"  zfs
}
zfs_acl_chmod_aclmode_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_aclmode_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_aclmode_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_compact_001_pos cleanup
zfs_acl_chmod_compact_001_pos_head()
{
	atf_set "descr" "chmod A{+|=} should set compact ACL correctly."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_compact_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_compact_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_compact_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_delete_001_pos cleanup
zfs_acl_chmod_delete_001_pos_head()
{
	atf_set "descr" "Verify that the combined delete_child/delete permission forowner/group/everyone are correct."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_delete_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_delete_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_delete_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_inherit_001_pos cleanup
zfs_acl_chmod_inherit_001_pos_head()
{
	atf_set "descr" "Verify chmod have correct behaviour to directory and file whensetting different inherit strategies to them."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_inherit_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_inherit_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_inherit_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_inherit_002_pos cleanup
zfs_acl_chmod_inherit_002_pos_head()
{
	atf_set "descr" "Verify chmod have correct behaviour to directory and file whenfilesystem has the different aclinherit setting."
	atf_set "require.config" zfs_acl
	atf_set "require.progs"  zfs
}
zfs_acl_chmod_inherit_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_inherit_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_inherit_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_inherit_003_pos cleanup
zfs_acl_chmod_inherit_003_pos_head()
{
	atf_set "descr" "Verify chmod have correct behaviour to directory and file whenfilesystem has the different aclinherit setting."
	atf_set "require.config" zfs_acl
	atf_set "require.progs"  zfs
}
zfs_acl_chmod_inherit_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_inherit_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_inherit_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_inherit_004_pos cleanup
zfs_acl_chmod_inherit_004_pos_head()
{
	atf_set "descr" "Verify aclinherit=passthrough-x will inherit the 'x' bits while mode request."
	atf_set "require.config" zfs_acl
	atf_set "require.progs"  zfs zpool
}
zfs_acl_chmod_inherit_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_inherit_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_inherit_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_owner_001_pos cleanup
zfs_acl_chmod_owner_001_pos_head()
{
	atf_set "descr" "Verify that the chown/chgrp could take owner/groupwhile permission is granted."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_owner_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_owner_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_owner_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_rwacl_001_pos cleanup
zfs_acl_chmod_rwacl_001_pos_head()
{
	atf_set "descr" "Verify chmod A[number]{+|-|=} read_acl/write_acl have correctbehaviour to access permission."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_rwacl_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_rwacl_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_rwacl_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_rwx_001_pos cleanup
zfs_acl_chmod_rwx_001_pos_head()
{
	atf_set "descr" "chmod A{+|-|=} have the correct behaviour to the ACL list."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_rwx_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_rwx_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_rwx_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_rwx_002_pos cleanup
zfs_acl_chmod_rwx_002_pos_head()
{
	atf_set "descr" "chmod A{+|-|=} read_data|write_data|execute for owner@, group@or everyone@ correctly alters mode bits."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_rwx_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_rwx_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_rwx_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_rwx_003_pos cleanup
zfs_acl_chmod_rwx_003_pos_head()
{
	atf_set "descr" "Verify that the read_data/write_data/execute permission forowner/group/everyone are correct."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_rwx_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_rwx_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_rwx_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_rwx_004_pos cleanup
zfs_acl_chmod_rwx_004_pos_head()
{
	atf_set "descr" "Verify that explicit ACL setting to specified user or group willoverride existed access rule."
	atf_set "require.config" zfs_acl
}
zfs_acl_chmod_rwx_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_rwx_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_rwx_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_xattr_001_pos cleanup
zfs_acl_chmod_xattr_001_pos_head()
{
	atf_set "descr" "Verify that the permission of read_xattr/write_xattr forowner/group/everyone are correct."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  runat
}
zfs_acl_chmod_xattr_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_xattr_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_xattr_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_chmod_xattr_002_pos cleanup
zfs_acl_chmod_xattr_002_pos_head()
{
	atf_set "descr" "Verify that the permission of write_xattr forowner/group/everyone while remove extended attributes are correct."
	atf_set "require.config" zfs_xattr
	atf_set "require.progs"  runat
}
zfs_acl_chmod_xattr_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_xattr_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_xattr_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_cp_001_pos cleanup
zfs_acl_cp_001_pos_head()
{
	atf_set "descr" "Verify that '$CP [-p]' supports ZFS ACLs."
	atf_set "require.config" zfs_acl
	atf_set "require.progs"  zfs
}
zfs_acl_cp_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_cp_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_cp_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_cp_002_pos cleanup
zfs_acl_cp_002_pos_head()
{
	atf_set "descr" "Verify that '$CP [-p]' supports ZFS ACLs."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  zfs runat
}
zfs_acl_cp_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_cp_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_cp_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_cpio_001_pos cleanup
zfs_acl_cpio_001_pos_head()
{
	atf_set "descr" "Verify that '$CPIO' command supports to archive ZFS ACLs."
	atf_set "require.config" zfs_acl
	atf_set "require.progs"  zfs
}
zfs_acl_cpio_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_cpio_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_cpio_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_cpio_002_pos cleanup
zfs_acl_cpio_002_pos_head()
{
	atf_set "descr" "Verify that '$CPIO' command supports to archive ZFS ACLs & xattrs."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  zfs runat
}
zfs_acl_cpio_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_cpio_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_cpio_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_find_001_pos cleanup
zfs_acl_find_001_pos_head()
{
	atf_set "descr" "Verify that '$FIND' command supports ZFS ACLs."
	atf_set "require.config" zfs_acl
}
zfs_acl_find_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_find_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_find_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_ls_001_pos cleanup
zfs_acl_ls_001_pos_head()
{
	atf_set "descr" "Verify that '$LS' command supports ZFS ACLs."
	atf_set "require.config" zfs_acl
}
zfs_acl_ls_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_ls_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_ls_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_mv_001_pos cleanup
zfs_acl_mv_001_pos_head()
{
	atf_set "descr" "Verify that '$MV' supports ZFS ACLs."
	atf_set "require.config" zfs_acl
}
zfs_acl_mv_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_mv_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_mv_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_tar_001_pos cleanup
zfs_acl_tar_001_pos_head()
{
	atf_set "descr" "Verify that '$TAR' command supports to archive ZFS ACLs."
	atf_set "require.config" zfs_acl
	atf_set "require.progs"  zfs
}
zfs_acl_tar_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_tar_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_tar_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_tar_002_pos cleanup
zfs_acl_tar_002_pos_head()
{
	atf_set "descr" "Verify that '$TAR' command supports to archive ZFS ACLs & xattrs."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  zfs runat
}
zfs_acl_tar_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_tar_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_tar_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_acl_chmod_001_neg
	atf_add_test_case zfs_acl_chmod_002_pos
	atf_add_test_case zfs_acl_chmod_aclmode_001_pos
	atf_add_test_case zfs_acl_chmod_compact_001_pos
	atf_add_test_case zfs_acl_chmod_delete_001_pos
	atf_add_test_case zfs_acl_chmod_inherit_001_pos
	atf_add_test_case zfs_acl_chmod_inherit_002_pos
	atf_add_test_case zfs_acl_chmod_inherit_003_pos
	atf_add_test_case zfs_acl_chmod_inherit_004_pos
	atf_add_test_case zfs_acl_chmod_owner_001_pos
	atf_add_test_case zfs_acl_chmod_rwacl_001_pos
	atf_add_test_case zfs_acl_chmod_rwx_001_pos
	atf_add_test_case zfs_acl_chmod_rwx_002_pos
	atf_add_test_case zfs_acl_chmod_rwx_003_pos
	atf_add_test_case zfs_acl_chmod_rwx_004_pos
	atf_add_test_case zfs_acl_chmod_xattr_001_pos
	atf_add_test_case zfs_acl_chmod_xattr_002_pos
	atf_add_test_case zfs_acl_cp_001_pos
	atf_add_test_case zfs_acl_cp_002_pos
	atf_add_test_case zfs_acl_cpio_001_pos
	atf_add_test_case zfs_acl_cpio_002_pos
	atf_add_test_case zfs_acl_find_001_pos
	atf_add_test_case zfs_acl_ls_001_pos
	atf_add_test_case zfs_acl_mv_001_pos
	atf_add_test_case zfs_acl_tar_001_pos
	atf_add_test_case zfs_acl_tar_002_pos
}
