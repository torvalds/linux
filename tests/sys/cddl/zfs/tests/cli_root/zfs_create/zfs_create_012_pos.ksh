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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfs_create_012_pos.ksh	1.2	09/01/13 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_upgrade/zfs_upgrade.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_create_012_pos
#
# DESCRIPTION:
# 'zfs create -p -o version=1' should only cause the leaf filesystem to be version=1
#
# STRATEGY:
# 1. Create $newdataset with -p option, verify it is created
# 2. Verify only the leaf filesystem to be version=1, others use the current version
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-08)
#
# __stc_assertion_end
#
################################################################################

if ! $(check_opt_support "create" "-p") ; then
	log_unsupported "-p option is not supported yet."
fi

if ! $(check_opt_support "upgrade"); then
	log_unsupported "zfs upgrade not supported yet."
fi

ZFS_VERSION=$($ZFS upgrade | $HEAD -1 | $AWK '{print $NF}' \
	| $SED -e 's/\.//g')

verify_runnable "both"

function cleanup
{
	if datasetexists $TESTPOOL/$TESTFS1 ; then
		log_must $ZFS destroy -rf $TESTPOOL/$TESTFS1
	fi
}

log_onexit cleanup


typeset newdataset1="$TESTPOOL/$TESTFS1/$TESTFS/$TESTFS1" 

log_assert "'zfs create -p -o version=1' only cause the leaf filesystem to be version=1."

log_must $ZFS create -p -o version=1 $newdataset1
log_must datasetexists $newdataset1

log_must check_fs_version $TESTPOOL/$TESTFS1/$TESTFS/$TESTFS1 1
for fs in $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1/$TESTFS ; do
	log_must check_fs_version $fs $ZFS_VERSION
done

log_pass "'zfs create -p -o version=1' only cause the leaf filesystem to be version=1."
