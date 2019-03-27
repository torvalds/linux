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
# ident	"@(#)zfs_clone_007_pos.ksh	1.1	09/01/13 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_clone_007_pos
#
# DESCRIPTION:
# 'zfs clone -o version=' could upgrade version, but downgrade is denied.
#
# STRATEGY:
# 1. Create clone with "-o version=" specified
# 2. Verify it succeed while upgrade, but fails while the version downgraded.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-12-16)
#
# __stc_assertion_end
#
################################################################################

if ! $(check_opt_support "upgrade") ; then
	log_unsupported "'zfs upgrade' unsupported."
fi

if ! $(check_opt_support "clone" "-o") ; then
	log_unsupported "'zfs clone -o' unsupported."
fi

ZFS_VERSION=$($ZFS upgrade | $HEAD -1 | $AWK '{print $NF}' \
	| $SED -e 's/\.//g')

verify_runnable "both"

function cleanup
{
	if snapexists $SNAPFS ; then
			log_must $ZFS destroy -Rf $SNAPFS
	fi
}

log_onexit cleanup

log_assert "'zfs clone -o version=' could upgrade version," \
	"but downgrade is denied."

log_must $ZFS snapshot $SNAPFS

typeset -i ver

if (( ZFS_TEST_VERSION == 0 )) ; then
	(( ZFS_TEST_VERSION = ZFS_VERSION ))
fi

(( ver = ZFS_TEST_VERSION ))
while (( ver <= ZFS_VERSION )); do
	log_must $ZFS clone -o version=$ver $SNAPFS $TESTPOOL/$TESTCLONE
	cleanup
	(( ver = ver + 1 ))
done

(( ver = 0 ))
while (( ver < ZFS_TEST_VERSION  )); do
	log_mustnot $ZFS clone -o version=$ver \
		$SNAPFS $TESTPOOL/$TESTCLONE
	log_mustnot datasetexists $TESTPOOL/$TESTCLONE
	cleanup
	(( ver = ver + 1 ))
done

log_pass "'zfs clone -o version=' could upgrade version," \
	"but downgrade is denied."
