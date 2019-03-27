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


atf_test_case iscsi_001_pos cleanup
iscsi_001_pos_head()
{
	atf_set "descr" "Verify that setting shareiscsi property on volume will make itan iSCSI target as expected."
	atf_set "require.progs"  zfs
}
iscsi_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/iscsi_001_pos.ksh || atf_fail "Testcase failed"
}
iscsi_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case iscsi_002_neg cleanup
iscsi_002_neg_head()
{
	atf_set "descr" "Verify file systems and snapshots can not be shared via iSCSI."
	atf_set "require.progs"  zfs
}
iscsi_002_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/iscsi_002_neg.ksh || atf_fail "Testcase failed"
}
iscsi_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case iscsi_003_neg cleanup
iscsi_003_neg_head()
{
	atf_set "descr" "Verify invalid value of shareiscsi can not be set"
	atf_set "require.progs"  zfs
}
iscsi_003_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/iscsi_003_neg.ksh || atf_fail "Testcase failed"
}
iscsi_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case iscsi_004_pos cleanup
iscsi_004_pos_head()
{
	atf_set "descr" "Verify renaming a volume does not change target's iSCSI name."
	atf_set "require.progs"  zfs
}
iscsi_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/iscsi_004_pos.ksh || atf_fail "Testcase failed"
}
iscsi_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case iscsi_005_pos cleanup
iscsi_005_pos_head()
{
	atf_set "descr" "Verify export/import have right effects on iSCSI targets."
	atf_set "require.progs"  zfs zpool
}
iscsi_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/iscsi_005_pos.ksh || atf_fail "Testcase failed"
}
iscsi_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case iscsi_006_neg cleanup
iscsi_006_neg_head()
{
	atf_set "descr" "Verify iscsioptions can not be changed by zfs command."
	atf_set "require.progs"  zfs
}
iscsi_006_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/iscsi_006_neg.ksh || atf_fail "Testcase failed"
}
iscsi_006_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/iscsi_common.kshlib
	. $(atf_get_srcdir)/iscsi.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case iscsi_001_pos
	atf_add_test_case iscsi_002_neg
	atf_add_test_case iscsi_003_neg
	atf_add_test_case iscsi_004_pos
	atf_add_test_case iscsi_005_pos
	atf_add_test_case iscsi_006_neg
}
