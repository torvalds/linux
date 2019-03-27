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
# Copyright 2012,2013 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#


atf_test_case zfsd_fault_001_pos cleanup
zfsd_fault_001_pos_head()
{
	atf_set "descr" "ZFS will fault a vdev that produces IO errors"
	atf_set "require.progs"  zfs zpool zfsd
	atf_set "timeout" 300
}
zfsd_fault_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_fault_001_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_fault_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfsd_degrade_001_pos cleanup
zfsd_degrade_001_pos_head()
{
	atf_set "descr" "ZFS will degrade a vdev that produces checksum errors"
	atf_set "require.progs"  zpool zfsd
	atf_set "timeout" 600
}
zfsd_degrade_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_degrade_001_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_degrade_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_degrade_002_pos cleanup
zfsd_degrade_002_pos_head()
{
	atf_set "descr" "ZFS will degrade a spare that produces checksum errors"
	atf_set "require.progs"  zpool zfsd
	atf_set "timeout" 600
}
zfsd_degrade_002_pos_body()
{
	atf_expect_fail "https://www.illumos.org/issues/8614 Checksum errors on a mirrored child of a raidz are incorrectly accounted"
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	verify_disk_count "$DISKS" 5
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_degrade_002_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_degrade_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfsd_hotspare_001_pos cleanup
zfsd_hotspare_001_pos_head()
{
	atf_set "descr" "An active, damaged spare will be replaced by an available spare"
	atf_set "require.progs"  zpool zfsd
	atf_set "timeout" 3600
}
zfsd_hotspare_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_hotspare_001_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_hotspare_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_hotspare_002_pos cleanup
zfsd_hotspare_002_pos_head()
{
	atf_set "descr" "If a vdev becomes degraded, the spare will be activated."
	atf_set "require.progs"  zpool zfsd zinject
	atf_set "timeout" 3600
}
zfsd_hotspare_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_hotspare_002_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_hotspare_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfsd_hotspare_003_pos cleanup
zfsd_hotspare_003_pos_head()
{
	atf_set "descr" "A faulted vdev will be replaced by an available spare"
	atf_set "require.progs"  zpool zfsd zinject
	atf_set "timeout" 3600
}
zfsd_hotspare_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	verify_disk_count "$DISKS" 5
	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_hotspare_003_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_hotspare_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_hotspare_004_pos cleanup
zfsd_hotspare_004_pos_head()
{
	atf_set "descr" "Removing a disk from a pool results in the spare activating"
	atf_set "require.progs"  gnop zpool camcontrol zfsd
	atf_set "timeout" 3600
}
zfsd_hotspare_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	verify_disk_count "$DISKS" 5
	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_hotspare_004_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_hotspare_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_hotspare_005_pos cleanup
zfsd_hotspare_005_pos_head()
{
	atf_set "descr" "A spare that is added to a degraded pool will be activated"
	atf_set "require.progs"  zpool zfsd zinject
	atf_set "timeout" 3600
}
zfsd_hotspare_005_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_hotspare_005_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_hotspare_005_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_hotspare_006_pos cleanup
zfsd_hotspare_006_pos_head()
{
	atf_set "descr" "zfsd will replace two vdevs that fail simultaneously"
	atf_set "require.progs"  zpool zfsd zinject
	atf_set "timeout" 3600
}
zfsd_hotspare_006_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_hotspare_006_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_hotspare_006_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_hotspare_007_pos cleanup
zfsd_hotspare_007_pos_head()
{
	atf_set "descr" "zfsd will swap failed drives at startup"
	atf_set "require.progs"  gnop zpool camcontrol zfsd
	atf_set "timeout" 3600
}
zfsd_hotspare_007_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	verify_disk_count "$DISKS" 5
	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_hotspare_007_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_hotspare_007_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_hotspare_008_neg cleanup
zfsd_hotspare_008_neg_head()
{
	atf_set "descr" "zfsd will not use newly added spares on replacing vdevs"
	atf_set "require.progs"  zpool zfsd
	atf_set "timeout" 3600
}
zfsd_hotspare_008_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	verify_disk_count "$DISKS" 4
	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_hotspare_008_neg.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_hotspare_008_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/hotspare_cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_autoreplace_001_neg cleanup
zfsd_autoreplace_001_neg_head()
{
	atf_set "descr" "A pool without autoreplace set will not replace by physical path"
	atf_set "require.progs"  zpool camcontrol zfsd gnop
	atf_set "timeout" 3600
}
zfsd_autoreplace_001_neg_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	verify_disk_count "$DISKS" 5
	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_autoreplace_001_neg.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_autoreplace_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_autoreplace_002_pos cleanup
zfsd_autoreplace_002_pos_head()
{
	atf_set "descr" "A pool with autoreplace set will replace by physical path"
	atf_set "require.progs"  gnop zpool zfsd
	atf_set "timeout" 3600
}
zfsd_autoreplace_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	verify_disk_count "$DISKS" 5
	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_autoreplace_002_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_autoreplace_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_autoreplace_003_pos cleanup
zfsd_autoreplace_003_pos_head()
{
	atf_set "descr" "A pool with autoreplace set will replace by physical path even if a spare is active"
	atf_set "require.progs"  zpool camcontrol zfsd gnop
	atf_set "timeout" 3600
}
zfsd_autoreplace_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	verify_disk_count "$DISKS" 5
	ksh93 $(atf_get_srcdir)/hotspare_setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_autoreplace_003_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_autoreplace_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_replace_001_pos cleanup
zfsd_replace_001_pos_head()
{
	atf_set "descr" "ZFSD will automatically replace a SAS disk that disappears and reappears in the same location, with the same devname"
	atf_set "require.progs"  zpool camcontrol zfsd zfs gnop
}
zfsd_replace_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	verify_disk_count "$DISKS" 3
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_replace_001_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_replace_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case zfsd_replace_002_pos cleanup
zfsd_replace_002_pos_head()
{
	atf_set "descr" "zfsd will reactivate a pool after all disks are failed and reappeared"
	atf_set "require.progs"  zpool camcontrol zfsd zfs
}
zfsd_replace_002_pos_body()
{
	atf_expect_fail "Not yet implemented in zfsd"
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	verify_disk_count "$DISKS" 3
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_replace_002_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_replace_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_replace_003_pos cleanup
zfsd_replace_003_pos_head()
{
	atf_set "descr" "ZFSD will correctly replace disks that dissapear and reappear with different devnames"
	atf_set "require.progs"  zpool camcontrol zfsd zfs gnop
}
zfsd_replace_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_replace_003_pos.ksh
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_replace_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/zfsd.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zfsd_import_001_pos cleanup
zfsd_import_001_pos_head()
{
	atf_set "descr" "If a removed drive gets reinserted while the pool is exported, it will detach its spare when imported."
	atf_set "require.progs"  gnop zfsd zpool
	atf_set "timeout" 3600
}
zfsd_import_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	verify_disk_count "$DISKS" 5
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zfsd_import_001_pos.ksh || atf_fail "Testcase failed"
	if [[ $? != 0 ]]; then
		save_artifacts
		atf_fail "Testcase failed"
	fi
}
zfsd_import_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/../hotspare/hotspare.kshlib
	. $(atf_get_srcdir)/../hotspare/hotspare.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}




atf_init_test_cases()
{
	atf_add_test_case zfsd_fault_001_pos
	atf_add_test_case zfsd_degrade_001_pos
	atf_add_test_case zfsd_degrade_002_pos
	atf_add_test_case zfsd_hotspare_001_pos
	atf_add_test_case zfsd_hotspare_002_pos
	atf_add_test_case zfsd_hotspare_003_pos
	atf_add_test_case zfsd_hotspare_004_pos
	atf_add_test_case zfsd_hotspare_005_pos
	atf_add_test_case zfsd_hotspare_006_pos
	atf_add_test_case zfsd_hotspare_007_pos
	atf_add_test_case zfsd_hotspare_008_neg
	atf_add_test_case zfsd_autoreplace_001_neg
	atf_add_test_case zfsd_autoreplace_002_pos
	atf_add_test_case zfsd_autoreplace_003_pos
	atf_add_test_case zfsd_replace_001_pos
	atf_add_test_case zfsd_replace_002_pos
	atf_add_test_case zfsd_replace_003_pos
	atf_add_test_case zfsd_import_001_pos
}

save_artifacts()
{
	# If ARTIFACTS_DIR is defined, save test artifacts for
	# post-mortem analysis
	if [[ -n $ARTIFACTS_DIR ]]; then
		TC_ARTIFACTS_DIR=${ARTIFACTS_DIR}/sys/cddl/zfs/tests/zfsd/$(atf_get ident)
		mkdir -p $TC_ARTIFACTS_DIR
		cp -a /var/log/zfsd.log* $TC_ARTIFACTS_DIR
		bzip2 $TC_ARTIFACTS_DIR/zfsd.log
	fi
}
