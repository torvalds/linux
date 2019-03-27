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


atf_test_case compress_001_pos cleanup
compress_001_pos_head()
{
	atf_set "descr" "Ensure that compressed files are smaller."
	atf_set "require.progs"  zfs
}
compress_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/compress.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/compress_001_pos.ksh || atf_fail "Testcase failed"
}
compress_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/compress.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case compress_003_pos cleanup
compress_003_pos_head()
{
	atf_set "descr" "Changing blocksize doesn't casue system panic with compression settings"
	atf_set "require.progs"  zfs
}
compress_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/compress.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/compress_003_pos.ksh || atf_fail "Testcase failed"
}
compress_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/compress.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case compress_004_pos cleanup
compress_004_pos_head()
{
	atf_set "descr" "Creating non-power-of-2 blocksize file and freeing the filestorage space at will should work normally with compression setting"
	atf_set "require.progs"  zfs
}
compress_004_pos_body()
{
	if [[ $(uname) = "FreeBSD" ]]; then
		atf_skip "FreeBSD does not implement F_FREESP in fcntl()"
	fi
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/compress.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/compress_004_pos.ksh || atf_fail "Testcase failed"
}
compress_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/compress.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case compress_001_pos
	atf_add_test_case compress_003_pos
	atf_add_test_case compress_004_pos
}
