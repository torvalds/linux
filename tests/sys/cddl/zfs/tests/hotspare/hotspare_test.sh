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


atf_test_case hotspare_add_001_pos cleanup
hotspare_add_001_pos_head()
{
	atf_set "descr" "'zpool add <pool> spare <vdev> ...' can add devices to the pool."
	atf_set "timeout" 3600
}
hotspare_add_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_add_001_pos.ksh || atf_fail "Testcase failed"
}
hotspare_add_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_add_002_pos cleanup
hotspare_add_002_pos_head()
{
	atf_set "descr" "'zpool add <pool> spare <vdev> ...' can add devices to the pool while it has spare-in device."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_add_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_add_002_pos.ksh || atf_fail "Testcase failed"
}
hotspare_add_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_add_003_neg cleanup
hotspare_add_003_neg_head()
{
	atf_set "descr" "'zpool add [-f]' with hot spares should fail with inapplicable scenarios."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_add_003_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_add_003_neg.ksh || atf_fail "Testcase failed"
}
hotspare_add_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_add_004_neg cleanup
hotspare_add_004_neg_head()
{
	atf_set "descr" "'zpool add [-f]' will not allow a swap device to be used as a hotspare'"
	atf_set "require.progs"  zpool swapon swapoff swapctl
}
hotspare_add_004_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_add_004_neg.ksh || atf_fail "Testcase failed"
}
hotspare_add_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_clone_001_pos cleanup
hotspare_clone_001_pos_head()
{
	atf_set "descr" "'zpool detach <pool> <vdev> ...' against hotspare should do no harm to clone."
	atf_set "require.progs"  zfs zpool sum
	atf_set "timeout" 3600
}
hotspare_clone_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_clone_001_pos.ksh || atf_fail "Testcase failed"
}
hotspare_clone_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_clone_002_pos cleanup
hotspare_clone_002_pos_head()
{
	atf_set "descr" "'zpool detach <pool> <vdev> ...' against basic vdev should do no harm to clone."
	atf_set "require.progs"  zfs zpool sum
	atf_set "timeout" 3600
}
hotspare_clone_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_clone_002_pos.ksh || atf_fail "Testcase failed"
}
hotspare_clone_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_create_001_neg cleanup
hotspare_create_001_neg_head()
{
	atf_set "descr" "'zpool create [-f]' with hot spares should be failedwith inapplicable scenarios."
	atf_set "require.progs"  dumpadm zpool
	atf_set "timeout" 3600
}
hotspare_create_001_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_create_001_neg.ksh || atf_fail "Testcase failed"
}
hotspare_create_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_detach_001_pos cleanup
hotspare_detach_001_pos_head()
{
	atf_set "descr" "'zpool detach <pool> <vdev> ...' should deactivate the spared-in hot spare device successfully."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_detach_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_detach_001_pos.ksh || atf_fail "Testcase failed"
}
hotspare_detach_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_detach_002_pos cleanup
hotspare_detach_002_pos_head()
{
	atf_set "descr" "'zpool detach <pool> <vdev> ...' against a functioning device that have spared should take the hot spare permanently swapping in successfully."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_detach_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_detach_002_pos.ksh || atf_fail "Testcase failed"
}
hotspare_detach_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_detach_003_pos cleanup
hotspare_detach_003_pos_head()
{
	atf_set "descr" "'zpool replace <pool> <vdev> <ndev>' against a functioning device that have spared should complete and the hot spare should return to available."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_detach_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_detach_003_pos.ksh || atf_fail "Testcase failed"
}
hotspare_detach_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_detach_004_pos cleanup
hotspare_detach_004_pos_head()
{
	atf_set "descr" "'zpool replace <pool> <vdev> <ndev>' against a hot spare device that have been activated should successful while the another dev is a available hot spare."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_detach_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_detach_004_pos.ksh || atf_fail "Testcase failed"
}
hotspare_detach_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_detach_005_neg cleanup
hotspare_detach_005_neg_head()
{
	atf_set "descr" "'zpool detach <pool> <vdev>' against a hot spare device that NOT activated should fail and issue an error message."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_detach_005_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_detach_005_neg.ksh || atf_fail "Testcase failed"
}
hotspare_detach_005_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_export_001_neg cleanup
hotspare_export_001_neg_head()
{
	atf_set "descr" "export pool that using shared hotspares will fail"
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_export_001_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_export_001_neg.ksh || atf_fail "Testcase failed"
}
hotspare_export_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_import_001_pos cleanup
hotspare_import_001_pos_head()
{
	atf_set "descr" "'zpool export/import <pool>' should runs successfully regardless the hotspare is only in list, activated, or offline."
	atf_set "require.progs"  zpool sum
	atf_set "timeout" 3600
}
hotspare_import_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_import_001_pos.ksh || atf_fail "Testcase failed"
}
hotspare_import_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_onoffline_003_neg cleanup
hotspare_onoffline_003_neg_head()
{
	atf_set "descr" "'zpool offline/online <pool> <vdev>' should fail on inactive spares"
	atf_set "require.progs"  zpool zdb
	atf_set "timeout" 3600
}
hotspare_onoffline_003_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_onoffline_003_neg.ksh || atf_fail "Testcase failed"
}
hotspare_onoffline_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_onoffline_004_neg cleanup
hotspare_onoffline_004_neg_head()
{
	atf_set "descr" "'zpool offline/online <pool> <vdev>' against a spared basic vdev during I/O completes."
	atf_set "require.progs"  zfs zpool zdb
	atf_set "timeout" 3600
}
hotspare_onoffline_004_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_onoffline_004_neg.ksh || atf_fail "Testcase failed"
}
hotspare_onoffline_004_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_remove_001_pos cleanup
hotspare_remove_001_pos_head()
{
	atf_set "descr" "'zpool remove <pool> <vdev> ...' can remove spare device from the pool."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_remove_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_remove_001_pos.ksh || atf_fail "Testcase failed"
}
hotspare_remove_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_remove_002_neg cleanup
hotspare_remove_002_neg_head()
{
	atf_set "descr" "'zpool remove <pool> <vdev> ...' should fail with inapplicable scenarios."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_remove_002_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_remove_002_neg.ksh || atf_fail "Testcase failed"
}
hotspare_remove_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_remove_003_neg cleanup
hotspare_remove_003_neg_head()
{
	atf_set "descr" "Executing 'zpool remove' with bad options fails"
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_remove_003_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_remove_003_neg.ksh || atf_fail "Testcase failed"
}
hotspare_remove_003_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_remove_004_pos cleanup
hotspare_remove_004_pos_head()
{
	atf_set "descr" "'zpool remove <pool> <vdev> ...' can remove spare device from the pool."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_remove_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_remove_004_pos.ksh || atf_fail "Testcase failed"
}
hotspare_remove_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_replace_001_neg cleanup
hotspare_replace_001_neg_head()
{
	atf_set "descr" "'zpool replace <pool> <odev> <ndev>' should fail with inapplicable scenarios."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_replace_001_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_replace_001_neg.ksh || atf_fail "Testcase failed"
}
hotspare_replace_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_replace_002_neg cleanup
hotspare_replace_002_neg_head()
{
	atf_set "descr" "'zpool replace <pool> <odev> <ndev>' should fail while the hot spares smaller than the basic vdev."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_replace_002_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_replace_002_neg.ksh || atf_fail "Testcase failed"
}
hotspare_replace_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_scrub_001_pos cleanup
hotspare_scrub_001_pos_head()
{
	atf_set "descr" "'zpool scrub <pool>' should runs successfully regardlessthe hotspare is only in list or activated."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_scrub_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_scrub_001_pos.ksh || atf_fail "Testcase failed"
}
hotspare_scrub_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_scrub_002_pos cleanup
hotspare_scrub_002_pos_head()
{
	atf_set "descr" "'zpool scrub' scans spare vdevs"
	atf_set "require.progs"  zpool
}
hotspare_scrub_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_scrub_002_pos.ksh || atf_fail "Testcase failed"
}
hotspare_scrub_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_shared_001_pos cleanup
hotspare_shared_001_pos_head()
{
	atf_set "descr" "'zpool add <pool> spare <vdev> ...' can add a disk as a shared spare to multiple pools."
	atf_set "require.progs"  zpool
	atf_set "timeout" 3600
}
hotspare_shared_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	verify_disk_count "$DISKS" 5
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_shared_001_pos.ksh || atf_fail "Testcase failed"
}
hotspare_shared_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_snapshot_001_pos cleanup
hotspare_snapshot_001_pos_head()
{
	atf_set "descr" "'zpool detach <pool> <vdev> ...' against hotspare should do no harm to snapshot."
	atf_set "require.progs"  zfs zpool sum
	atf_set "timeout" 3600
}
hotspare_snapshot_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_snapshot_001_pos.ksh || atf_fail "Testcase failed"
}
hotspare_snapshot_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case hotspare_snapshot_002_pos cleanup
hotspare_snapshot_002_pos_head()
{
	atf_set "descr" "'zpool detach <pool> <vdev> ...' against basic vdev do no harm to snapshot."
	atf_set "require.progs"  zfs zpool sum
	atf_set "timeout" 3600
}
hotspare_snapshot_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/hotspare_snapshot_002_pos.ksh || atf_fail "Testcase failed"
}
hotspare_snapshot_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/hotspare.kshlib
	. $(atf_get_srcdir)/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case hotspare_add_001_pos
	atf_add_test_case hotspare_add_002_pos
	atf_add_test_case hotspare_add_003_neg
	atf_add_test_case hotspare_add_004_neg
	atf_add_test_case hotspare_clone_001_pos
	atf_add_test_case hotspare_clone_002_pos
	atf_add_test_case hotspare_create_001_neg
	atf_add_test_case hotspare_detach_001_pos
	atf_add_test_case hotspare_detach_002_pos
	atf_add_test_case hotspare_detach_003_pos
	atf_add_test_case hotspare_detach_004_pos
	atf_add_test_case hotspare_detach_005_neg
	atf_add_test_case hotspare_export_001_neg
	atf_add_test_case hotspare_import_001_pos
	atf_add_test_case hotspare_onoffline_003_neg
	atf_add_test_case hotspare_onoffline_004_neg
	atf_add_test_case hotspare_remove_001_pos
	atf_add_test_case hotspare_remove_002_neg
	atf_add_test_case hotspare_remove_003_neg
	atf_add_test_case hotspare_remove_004_pos
	atf_add_test_case hotspare_replace_001_neg
	atf_add_test_case hotspare_replace_002_neg
	atf_add_test_case hotspare_scrub_001_pos
	atf_add_test_case hotspare_scrub_002_pos
	atf_add_test_case hotspare_shared_001_pos
	atf_add_test_case hotspare_snapshot_001_pos
	atf_add_test_case hotspare_snapshot_002_pos
}
