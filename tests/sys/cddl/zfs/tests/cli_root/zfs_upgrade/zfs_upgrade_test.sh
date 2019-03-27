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


atf_test_case zfs_upgrade_001_pos cleanup
zfs_upgrade_001_pos_head()
{
	atf_set "descr" "Executing 'zfs upgrade' command succeeds."
	atf_set "require.progs"  zfs nawk
}
zfs_upgrade_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_upgrade_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_upgrade_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_upgrade_002_pos cleanup
zfs_upgrade_002_pos_head()
{
	atf_set "descr" "Executing 'zfs upgrade -v' command succeeds."
	atf_set "require.progs"  zfs nawk
}
zfs_upgrade_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_upgrade_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_upgrade_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_upgrade_003_pos cleanup
zfs_upgrade_003_pos_head()
{
	atf_set "descr" "Executing 'zfs upgrade [-V version] filesystem' command succeeds."
	atf_set "require.progs"  zfs
}
zfs_upgrade_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_upgrade_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_upgrade_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_upgrade_004_pos cleanup
zfs_upgrade_004_pos_head()
{
	atf_set "descr" "Executing 'zfs upgrade -r [-V version] filesystem' command succeeds."
	atf_set "require.progs"  zfs
}
zfs_upgrade_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_upgrade_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_upgrade_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_upgrade_005_pos cleanup
zfs_upgrade_005_pos_head()
{
	atf_set "descr" "Executing 'zfs upgrade [-V version] -a' command succeeds."
	atf_set "require.progs"  zfs
}
zfs_upgrade_005_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	if other_pools_exist; then
                atf_skip "Can't test unmount -a with existing pools"
        fi

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_upgrade_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_upgrade_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_upgrade_006_neg cleanup
zfs_upgrade_006_neg_head()
{
	atf_set "descr" "Badly-formed 'zfs upgrade' should return an error."
	atf_set "require.progs"  zfs
}
zfs_upgrade_006_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_upgrade_006_neg.ksh || atf_fail "Testcase failed"
}
zfs_upgrade_006_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_upgrade_007_neg cleanup
zfs_upgrade_007_neg_head()
{
	atf_set "descr" "Set invalid value or non-digit version should fail as expected."
	atf_set "require.progs"  zfs
}
zfs_upgrade_007_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_upgrade_007_neg.ksh || atf_fail "Testcase failed"
}
zfs_upgrade_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zfs_upgrade.cfg
	. $(atf_get_srcdir)/zfs_upgrade.kshlib

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_upgrade_001_pos
	atf_add_test_case zfs_upgrade_002_pos
	atf_add_test_case zfs_upgrade_003_pos
	atf_add_test_case zfs_upgrade_004_pos
	atf_add_test_case zfs_upgrade_005_pos
	atf_add_test_case zfs_upgrade_006_neg
	atf_add_test_case zfs_upgrade_007_neg
}
