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


atf_test_case zfs_acl_chmod_001_pos cleanup
zfs_acl_chmod_001_pos_head()
{
	atf_set "descr" "Verify chmod permission settings on files and directories"
}
zfs_acl_chmod_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_chmod_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_chmod_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_compress_001_pos cleanup
zfs_acl_compress_001_pos_head()
{
	atf_set "descr" "Compress will keep file attribute intact after the file iscompressed and uncompressed"
	atf_set "require.config" zfs_acl zfs_xattr
}
zfs_acl_compress_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_compress_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_compress_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_cp_001_pos cleanup
zfs_acl_cp_001_pos_head()
{
	atf_set "descr" "Verifies that cp will include file attribute when using the -@ flag"
	atf_set "require.config" zfs_acl zfs_xattr
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


atf_test_case zfs_acl_cp_002_neg cleanup
zfs_acl_cp_002_neg_head()
{
	atf_set "descr" "Verifies that cp will not include file attribute when the -@ flagis not present."
	atf_set "require.config" zfs_acl zfs_xattr
}
zfs_acl_cp_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_cp_002_neg.ksh || atf_fail "Testcase failed"
}
zfs_acl_cp_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_cp_003_neg cleanup
zfs_acl_cp_003_neg_head()
{
	atf_set "descr" "Verifies that cp won't be able to include file attribute whenattribute is unreadable (except root)"
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  runat
}
zfs_acl_cp_003_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_cp_003_neg.ksh || atf_fail "Testcase failed"
}
zfs_acl_cp_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_find_001_pos cleanup
zfs_acl_find_001_pos_head()
{
	atf_set "descr" "Verifies ability to find files with attribute with-xattr flag and using '-exec runat ls'"
	atf_set "require.config" zfs_acl zfs_xattr
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


atf_test_case zfs_acl_find_002_neg cleanup
zfs_acl_find_002_neg_head()
{
	atf_set "descr" "verifies -xattr doesn't include files withoutattribute and using '-exec runat ls'"
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  runat
}
zfs_acl_find_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_find_002_neg.ksh || atf_fail "Testcase failed"
}
zfs_acl_find_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_ls_001_pos cleanup
zfs_acl_ls_001_pos_head()
{
	atf_set "descr" "Verifies that ls displays @ in the file permissions using ls -@for files with attribute."
	atf_set "require.config" zfs_acl zfs_xattr
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


atf_test_case zfs_acl_ls_002_neg cleanup
zfs_acl_ls_002_neg_head()
{
	atf_set "descr" "Verifies that ls doesn't display @ in the filepermissions using ls -@ for files without attribute."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  runat
}
zfs_acl_ls_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_ls_002_neg.ksh || atf_fail "Testcase failed"
}
zfs_acl_ls_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_mv_001_pos cleanup
zfs_acl_mv_001_pos_head()
{
	atf_set "descr" "Verifies that mv will include file attribute."
	atf_set "require.config" zfs_acl zfs_xattr
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


atf_test_case zfs_acl_pack_001_pos cleanup
zfs_acl_pack_001_pos_head()
{
	atf_set "descr" "Verifies that pack will keep file attribute intact after the fileis packed and unpacked"
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  unpack pack
}
zfs_acl_pack_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_pack_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_pack_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_pax_001_pos cleanup
zfs_acl_pax_001_pos_head()
{
	atf_set "descr" "Verify include attribute in pax archive and restore with paxshould succeed."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  pax
}
zfs_acl_pax_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_pax_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_pax_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_pax_002_pos cleanup
zfs_acl_pax_002_pos_head()
{
	atf_set "descr" "Verify include attribute in pax archive and restore with tarshould succeed."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  pax
}
zfs_acl_pax_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_pax_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_pax_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_pax_003_pos cleanup
zfs_acl_pax_003_pos_head()
{
	atf_set "descr" "Verify include attribute in pax archive and restore with cpioshould succeed."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  pax
}
zfs_acl_pax_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_pax_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_pax_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_pax_004_pos cleanup
zfs_acl_pax_004_pos_head()
{
	atf_set "descr" "Verify files include attribute in pax archive and restore with paxshould succeed."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  pax
}
zfs_acl_pax_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_pax_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_pax_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_pax_005_pos cleanup
zfs_acl_pax_005_pos_head()
{
	atf_set "descr" "Verify files include attribute in cpio archive and restore withcpio should succeed."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  pax
}
zfs_acl_pax_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_pax_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_pax_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_pax_006_pos cleanup
zfs_acl_pax_006_pos_head()
{
	atf_set "descr" "Verify files include attribute in tar archive and restore withtar should succeed."
	atf_set "require.config" zfs_acl zfs_xattr
	atf_set "require.progs"  pax
}
zfs_acl_pax_006_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_pax_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_acl_pax_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_acl_tar_001_pos cleanup
zfs_acl_tar_001_pos_head()
{
	atf_set "descr" "Verifies that tar will include file attribute when @ flag ispresent."
	atf_set "require.config" zfs_acl zfs_xattr
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


atf_test_case zfs_acl_tar_002_neg cleanup
zfs_acl_tar_002_neg_head()
{
	atf_set "descr" "Verifies that tar will not include files attribute when @ flag isnot present"
	atf_set "require.config" zfs_acl zfs_xattr
}
zfs_acl_tar_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/../setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_acl_tar_002_neg.ksh || atf_fail "Testcase failed"
}
zfs_acl_tar_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/../acl.cfg

	ksh93 $(atf_get_srcdir)/../cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_acl_chmod_001_pos
	atf_add_test_case zfs_acl_compress_001_pos
	atf_add_test_case zfs_acl_cp_001_pos
	atf_add_test_case zfs_acl_cp_002_neg
	atf_add_test_case zfs_acl_cp_003_neg
	atf_add_test_case zfs_acl_find_001_pos
	atf_add_test_case zfs_acl_find_002_neg
	atf_add_test_case zfs_acl_ls_001_pos
	atf_add_test_case zfs_acl_ls_002_neg
	atf_add_test_case zfs_acl_mv_001_pos
	atf_add_test_case zfs_acl_pack_001_pos
	atf_add_test_case zfs_acl_pax_001_pos
	atf_add_test_case zfs_acl_pax_002_pos
	atf_add_test_case zfs_acl_pax_003_pos
	atf_add_test_case zfs_acl_pax_004_pos
	atf_add_test_case zfs_acl_pax_005_pos
	atf_add_test_case zfs_acl_pax_006_pos
	atf_add_test_case zfs_acl_tar_001_pos
	atf_add_test_case zfs_acl_tar_002_neg
}
