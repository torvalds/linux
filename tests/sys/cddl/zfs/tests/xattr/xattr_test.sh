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


atf_test_case xattr_001_pos cleanup
xattr_001_pos_head()
{
	atf_set "descr" "Create/read/write/append of xattrs works"
	atf_set "require.progs"  svcadm svcs
}
xattr_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_001_pos.ksh || atf_fail "Testcase failed"
}
xattr_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_002_neg cleanup
xattr_002_neg_head()
{
	atf_set "descr" "A read of a non-existent xattr fails"
	atf_set "require.progs"  svcadm svcs
}
xattr_002_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_002_neg.ksh || atf_fail "Testcase failed"
}
xattr_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_003_neg cleanup
xattr_003_neg_head()
{
	atf_set "descr" "read/write xattr on a file with no permissions fails"
	atf_set "require.progs"  svcs svcadm runat runwattr
}
xattr_003_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_003_neg.ksh || atf_fail "Testcase failed"
}
xattr_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_004_pos cleanup
xattr_004_pos_head()
{
	atf_set "descr" "Files from ufs,tmpfs with xattrs copied to zfs retain xattr info."
	atf_set "require.progs"  zfs svcadm runat svcs
}
xattr_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_004_pos.ksh || atf_fail "Testcase failed"
}
xattr_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_005_pos cleanup
xattr_005_pos_head()
{
	atf_set "descr" "read/write/create/delete xattr on a clone filesystem"
	atf_set "require.progs"  zfs svcadm svcs
}
xattr_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_005_pos.ksh || atf_fail "Testcase failed"
}
xattr_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_006_pos cleanup
xattr_006_pos_head()
{
	atf_set "descr" "read xattr on a snapshot"
	atf_set "require.progs"  zfs svcadm svcs
}
xattr_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_006_pos.ksh || atf_fail "Testcase failed"
}
xattr_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_007_neg cleanup
xattr_007_neg_head()
{
	atf_set "descr" "create/write xattr on a snapshot fails"
	atf_set "require.progs"  zfs svcadm runat svcs
}
xattr_007_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_007_neg.ksh || atf_fail "Testcase failed"
}
xattr_007_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_008_pos cleanup
xattr_008_pos_head()
{
	atf_set "descr" "special . and .. dirs work as expected for xattrs"
	atf_set "require.progs"  svcadm runat svcs
}
xattr_008_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_008_pos.ksh || atf_fail "Testcase failed"
}
xattr_008_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_009_neg cleanup
xattr_009_neg_head()
{
	atf_set "descr" "links between xattr and normal file namespace fail"
	atf_set "require.progs"  svcadm runat svcs
}
xattr_009_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_009_neg.ksh || atf_fail "Testcase failed"
}
xattr_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_010_neg cleanup
xattr_010_neg_head()
{
	atf_set "descr" "mkdir, mknod fail"
	atf_set "require.progs"  svcadm runat svcs
}
xattr_010_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_010_neg.ksh || atf_fail "Testcase failed"
}
xattr_010_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_011_pos cleanup
xattr_011_pos_head()
{
	atf_set "descr" "Basic applications work with xattrs: cpio cp find mv pax tar"
	atf_set "require.progs"  pax svcadm runat svcs
}
xattr_011_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_011_pos.ksh || atf_fail "Testcase failed"
}
xattr_011_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_012_pos cleanup
xattr_012_pos_head()
{
	atf_set "descr" "xattr file sizes count towards normal disk usage"
	atf_set "require.progs"  svcadm zfs runat zpool svcs
}
xattr_012_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_012_pos.ksh || atf_fail "Testcase failed"
}
xattr_012_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case xattr_013_pos cleanup
xattr_013_pos_head()
{
	atf_set "descr" "The noxattr mount option functions as expected"
	atf_set "require.progs"  zfs svcadm runat svcs
}
xattr_013_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/xattr_013_pos.ksh || atf_fail "Testcase failed"
}
xattr_013_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/xattr_common.kshlib
	. $(atf_get_srcdir)/xattr.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case xattr_001_pos
	atf_add_test_case xattr_002_neg
	atf_add_test_case xattr_003_neg
	atf_add_test_case xattr_004_pos
	atf_add_test_case xattr_005_pos
	atf_add_test_case xattr_006_pos
	atf_add_test_case xattr_007_neg
	atf_add_test_case xattr_008_pos
	atf_add_test_case xattr_009_neg
	atf_add_test_case xattr_010_neg
	atf_add_test_case xattr_011_pos
	atf_add_test_case xattr_012_pos
	atf_add_test_case xattr_013_pos
}
