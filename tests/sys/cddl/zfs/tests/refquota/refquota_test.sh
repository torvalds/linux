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


atf_test_case refquota_001_pos cleanup
refquota_001_pos_head()
{
	atf_set "descr" "refquota limits the amount of space a dataset can consume,but does not include space used by descendents."
	atf_set "require.progs"  zfs
}
refquota_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/refquota_001_pos.ksh || atf_fail "Testcase failed"
}
refquota_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case refquota_002_pos cleanup
refquota_002_pos_head()
{
	atf_set "descr" "Quotas are enforced using the minimum of the two properties"
	atf_set "require.progs"  zfs
}
refquota_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/refquota_002_pos.ksh || atf_fail "Testcase failed"
}
refquota_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case refquota_003_pos cleanup
refquota_003_pos_head()
{
	atf_set "descr" "Sub-filesystem quotas are not enforced by property 'refquota'"
	atf_set "require.progs"  zfs
}
refquota_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/refquota_003_pos.ksh || atf_fail "Testcase failed"
}
refquota_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case refquota_004_pos cleanup
refquota_004_pos_head()
{
	atf_set "descr" "refquotas are not limited by snapshots."
	atf_set "require.progs"  zfs
}
refquota_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/refquota_004_pos.ksh || atf_fail "Testcase failed"
}
refquota_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case refquota_005_pos cleanup
refquota_005_pos_head()
{
	atf_set "descr" "refquotas are not limited by sub-filesystem snapshots."
	atf_set "require.progs"  zfs
}
refquota_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/refquota_005_pos.ksh || atf_fail "Testcase failed"
}
refquota_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case refquota_006_neg cleanup
refquota_006_neg_head()
{
	atf_set "descr" "'zfs set refquota' can handle incorrect arguments correctly."
	atf_set "require.progs"  zfs
}
refquota_006_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/refquota_006_neg.ksh || atf_fail "Testcase failed"
}
refquota_006_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/refquota.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case refquota_001_pos
	atf_add_test_case refquota_002_pos
	atf_add_test_case refquota_003_pos
	atf_add_test_case refquota_004_pos
	atf_add_test_case refquota_005_pos
	atf_add_test_case refquota_006_neg
}
