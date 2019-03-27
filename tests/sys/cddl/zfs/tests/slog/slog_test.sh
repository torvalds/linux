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


atf_test_case slog_001_pos cleanup
slog_001_pos_head()
{
	atf_set "descr" "Creating a pool with a log device succeeds."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_001_pos.ksh || atf_fail "Testcase failed"
}
slog_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_002_pos cleanup
slog_002_pos_head()
{
	atf_set "descr" "Adding a log device to normal pool works."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_002_pos.ksh || atf_fail "Testcase failed"
}
slog_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_003_pos cleanup
slog_003_pos_head()
{
	atf_set "descr" "Adding an extra log device works."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_003_pos.ksh || atf_fail "Testcase failed"
}
slog_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_004_pos cleanup
slog_004_pos_head()
{
	atf_set "descr" "Attaching a log device passes."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_004_pos.ksh || atf_fail "Testcase failed"
}
slog_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_005_pos cleanup
slog_005_pos_head()
{
	atf_set "descr" "Detaching a log device passes."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_005_pos.ksh || atf_fail "Testcase failed"
}
slog_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_006_pos cleanup
slog_006_pos_head()
{
	atf_set "descr" "Replacing a log device passes."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_006_pos.ksh || atf_fail "Testcase failed"
}
slog_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_007_pos cleanup
slog_007_pos_head()
{
	atf_set "descr" "Exporting and importing pool with log devices passes."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_007_pos.ksh || atf_fail "Testcase failed"
}
slog_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_008_neg cleanup
slog_008_neg_head()
{
	atf_set "descr" "A raidz/raidz2 log is not supported."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_008_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_008_neg.ksh || atf_fail "Testcase failed"
}
slog_008_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_009_neg cleanup
slog_009_neg_head()
{
	atf_set "descr" "A raidz/raidz2 log can not be added to existed pool."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_009_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_009_neg.ksh || atf_fail "Testcase failed"
}
slog_009_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_010_neg cleanup
slog_010_neg_head()
{
	atf_set "descr" "Slog device can not be replaced with spare device."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_010_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_010_neg.ksh || atf_fail "Testcase failed"
}
slog_010_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_011_neg cleanup
slog_011_neg_head()
{
	atf_set "descr" "Offline and online a log device passes."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_011_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_011_neg.ksh || atf_fail "Testcase failed"
}
slog_011_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_012_neg cleanup
slog_012_neg_head()
{
	atf_set "descr" "Pool can survive when one of mirror log device get corrupted."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_012_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_012_neg.ksh || atf_fail "Testcase failed"
}
slog_012_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_013_pos cleanup
slog_013_pos_head()
{
	atf_set "descr" "Verify slog device can be disk, file, lofi device or any devicethat presents a block interface."
	atf_set "require.progs"  zpool lofiadm
	atf_set "timeout" 1200
}
slog_013_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_013_pos.ksh || atf_fail "Testcase failed"
}
slog_013_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case slog_014_pos cleanup
slog_014_pos_head()
{
	atf_set "descr" "log device can survive when one of the pool device get corrupted."
	atf_set "require.progs"  zpool
	atf_set "timeout" 1200
}
slog_014_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/slog_014_pos.ksh || atf_fail "Testcase failed"
}
slog_014_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/slog.kshlib
	. $(atf_get_srcdir)/slog.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case slog_001_pos
	atf_add_test_case slog_002_pos
	atf_add_test_case slog_003_pos
	atf_add_test_case slog_004_pos
	atf_add_test_case slog_005_pos
	atf_add_test_case slog_006_pos
	atf_add_test_case slog_007_pos
	atf_add_test_case slog_008_neg
	atf_add_test_case slog_009_neg
	atf_add_test_case slog_010_neg
	atf_add_test_case slog_011_neg
	atf_add_test_case slog_012_neg
	atf_add_test_case slog_013_pos
	atf_add_test_case slog_014_pos
}
