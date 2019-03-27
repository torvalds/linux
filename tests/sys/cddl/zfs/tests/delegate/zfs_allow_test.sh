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


atf_test_case zfs_allow_001_pos cleanup
zfs_allow_001_pos_head()
{
	atf_set "descr" "everyone' is interpreted as a keyword even if a useror group named 'everyone' exists."
	atf_set "require.progs"  zfs svcs
}
zfs_allow_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_001_pos.ksh || atf_fail "Testcase failed"
}
zfs_allow_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_002_pos cleanup
zfs_allow_002_pos_head()
{
	atf_set "descr" "<user|group> is interpreted as user if possible, then as group."
	atf_set "require.progs"  zfs svcs
}
zfs_allow_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_002_pos.ksh || atf_fail "Testcase failed"
}
zfs_allow_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_003_pos cleanup
zfs_allow_003_pos_head()
{
	atf_set "descr" "Verify option '-l' only allow permission to the dataset itself."
	atf_set "require.progs"  zfs svcs
}
zfs_allow_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_003_pos.ksh || atf_fail "Testcase failed"
}
zfs_allow_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_004_pos cleanup
zfs_allow_004_pos_head()
{
	atf_set "descr" "Verify option '-d' allow permission to the descendent datasets."
	atf_set "require.progs"  zfs svcs
}
zfs_allow_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_004_pos.ksh || atf_fail "Testcase failed"
}
zfs_allow_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_005_pos cleanup
zfs_allow_005_pos_head()
{
	atf_set "descr" "Verify option '-c' will be granted locally to the creator."
	atf_set "require.progs"  zfs svcs runwattr
}
zfs_allow_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_005_pos.ksh || atf_fail "Testcase failed"
}
zfs_allow_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_006_pos cleanup
zfs_allow_006_pos_head()
{
	atf_set "descr" "Changing permissions in a set will change what is allowedwherever the set is used."
	atf_set "require.progs"  zfs svcs
}
zfs_allow_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_006_pos.ksh || atf_fail "Testcase failed"
}
zfs_allow_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_007_pos cleanup
zfs_allow_007_pos_head()
{
	atf_set "descr" "Verify permission set can be masked on descendent dataset."
	atf_set "require.progs"  zfs svcs
}
zfs_allow_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_007_pos.ksh || atf_fail "Testcase failed"
}
zfs_allow_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_008_pos cleanup
zfs_allow_008_pos_head()
{
	atf_set "descr" "Verify non-root user can allow permissions."
	atf_set "require.progs"  zfs svcs runwattr
}
zfs_allow_008_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_008_pos.ksh || atf_fail "Testcase failed"
}
zfs_allow_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_009_neg cleanup
zfs_allow_009_neg_head()
{
	atf_set "descr" "Verify invalid arguments are handled correctly."
	atf_set "require.progs"  zfs svcs
}
zfs_allow_009_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_009_neg.ksh || atf_fail "Testcase failed"
}
zfs_allow_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_010_pos cleanup
zfs_allow_010_pos_head()
{
	atf_set "descr" "Verify privileged user has correct permissions once which wasdelegated to him in datasets"
	atf_set "require.progs"  zfs svcs
}
zfs_allow_010_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_010_pos.ksh || atf_fail "Testcase failed"
}
zfs_allow_010_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_011_neg cleanup
zfs_allow_011_neg_head()
{
	atf_set "descr" "Verify zpool subcmds and system readonly properties can't bedelegated."
	atf_set "require.progs"  zfs svcs
}
zfs_allow_011_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_011_neg.ksh || atf_fail "Testcase failed"
}
zfs_allow_011_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfs_allow_012_neg cleanup
zfs_allow_012_neg_head()
{
	atf_set "descr" "Verify privileged user can not use permissions properly whendelegation property is set off"
	atf_set "require.progs"  zfs zpool svcs
}
zfs_allow_012_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfs_allow_012_neg.ksh || atf_fail "Testcase failed"
}
zfs_allow_012_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/delegate_common.kshlib
	. $(atf_get_srcdir)/delegate.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zfs_allow_001_pos
	atf_add_test_case zfs_allow_002_pos
	atf_add_test_case zfs_allow_003_pos
	atf_add_test_case zfs_allow_004_pos
	atf_add_test_case zfs_allow_005_pos
	atf_add_test_case zfs_allow_006_pos
	atf_add_test_case zfs_allow_007_pos
	atf_add_test_case zfs_allow_008_pos
	atf_add_test_case zfs_allow_009_neg
	atf_add_test_case zfs_allow_010_pos
	atf_add_test_case zfs_allow_011_neg
	atf_add_test_case zfs_allow_012_neg
}
