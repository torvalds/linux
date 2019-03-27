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


atf_test_case zpool_upgrade_001_pos cleanup
zpool_upgrade_001_pos_head()
{
	atf_set "descr" "Executing 'zpool upgrade -v' command succeeds."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
zpool_upgrade_001_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_001_pos.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_upgrade_002_pos cleanup
zpool_upgrade_002_pos_head()
{
	atf_set "descr" "Import pools of all versions - zpool upgrade on each pools works"
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
zpool_upgrade_002_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_002_pos.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_upgrade_003_pos cleanup
zpool_upgrade_003_pos_head()
{
	atf_set "descr" "Upgrading a pool that has already been upgraded succeeds."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
zpool_upgrade_003_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_003_pos.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_upgrade_004_pos cleanup
zpool_upgrade_004_pos_head()
{
	atf_set "descr" "zpool upgrade -a works"
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
zpool_upgrade_004_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	verify_disk_count "$DISKS" 2
	if other_pools_exist; then
                atf_skip "Can't test unmount -a with existing pools"
        fi

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_004_pos.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_upgrade_005_neg cleanup
zpool_upgrade_005_neg_head()
{
	atf_set "descr" "Variations of upgrade -v print usage message,return with non-zero status"
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
zpool_upgrade_005_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_005_neg.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_upgrade_006_neg cleanup
zpool_upgrade_006_neg_head()
{
	atf_set "descr" "Attempting to upgrade a non-existent pool will return an error"
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
zpool_upgrade_006_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_006_neg.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_006_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_upgrade_007_pos cleanup
zpool_upgrade_007_pos_head()
{
	atf_set "descr" "Import pools of all versions - 'zfs upgrade' on each pools works"
	atf_set "require.progs"  zpool
	# This test can take quite a while, especially on debug builds
	atf_set "timeout" 7200
}
zpool_upgrade_007_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_007_pos.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_upgrade_008_pos cleanup
zpool_upgrade_008_pos_head()
{
	atf_set "descr" "Zpool upgrade should be able to upgrade pools to a given version using -V"
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
zpool_upgrade_008_pos_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_008_pos.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zpool_upgrade_009_neg cleanup
zpool_upgrade_009_neg_head()
{
	atf_set "descr" "Zpool upgrade -V shouldn't be able to upgrade a pool to an unknown version"
	atf_set "require.progs"  zpool
	atf_set "timeout" 1800
}
zpool_upgrade_009_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_upgrade_009_neg.ksh || atf_fail "Testcase failed"
}
zpool_upgrade_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_upgrade.kshlib
	. $(atf_get_srcdir)/zpool_upgrade.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zpool_upgrade_001_pos
	atf_add_test_case zpool_upgrade_002_pos
	atf_add_test_case zpool_upgrade_003_pos
	atf_add_test_case zpool_upgrade_004_pos
	atf_add_test_case zpool_upgrade_005_neg
	atf_add_test_case zpool_upgrade_006_neg
	atf_add_test_case zpool_upgrade_007_pos
	atf_add_test_case zpool_upgrade_008_pos
	atf_add_test_case zpool_upgrade_009_neg
}
