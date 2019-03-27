#!/usr/local/bin/ksh93 -p
#
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_upgrade_007_pos.ksh	1.3	08/08/15 SMI"
#
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_upgrade/zpool_upgrade.kshlib
. $STF_SUITE/tests/cli_root/zfs_upgrade/zfs_upgrade.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_upgrade_007_pos
#
# DESCRIPTION:
# import pools of all versions - verify the following operation not break.
#	* zfs create -o version=<vers> <filesystem>
#	* zfs upgrade [-V vers] <filesystem>
#	* zfs set version=<vers> <filesystem>
#
# STRATEGY:
# 1. Import pools of all versions
# 2. Setup a test enviorment over the old pools.
# 3. Verify the commands related to 'zfs upgrade' succeed as expected.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-28)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

if ! fs_prop_exist "version" ; then
	log_unsupported "version is not supported by this release."
fi

function cleanup
{
	destroy_upgraded_pool $config
}

log_assert "Import pools of all versions - 'zfs upgrade' on each pools works"
log_onexit cleanup

# $CONFIGS gets set in the .cfg script
for config in $CONFIGS
do
	create_old_pool $config
	pool=$($ENV| $GREP "ZPOOL_VERSION_${config}_NAME" \
                | $AWK -F= '{print $2}')

	default_check_zfs_upgrade $pool
	destroy_upgraded_pool $config
done

log_pass "Import pools of all versions - 'zfs upgrade' on each pools works"
