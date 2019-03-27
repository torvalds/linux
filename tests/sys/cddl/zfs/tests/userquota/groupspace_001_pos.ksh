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
# ident	"@(#)groupspace_001_pos.ksh	1.2	09/08/06 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: groupspace_001_pos
#
# DESCRIPTION:
#       Check the zfs groupspace with all parameters
#
#
# STRATEGY:
#       1. set zfs groupquota to a fs
#       2. write some data to the fs with specified user and group
#	3. use zfs groupspace with all possible parameters to check the result
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2009-04-16)
#
# __stc_assertion_end
#
###############################################################################

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

function cleanup
{
	if datasetexists $snap_fs; then
		log_must $ZFS destroy $snap_fs
	fi

	log_must cleanup_quota	
}

log_onexit cleanup

log_assert "Check the zfs groupspace with all possible parameters"

set -A params -- "-h" "--help" "-n" "-H" "-p" "-o type,name,used,quota" \
	"-o name,used,quota" "-o used,quota" "-o used" "-o quota" \
	"-s type" "-s name" "-s used" "-s quota" \
	"-S type" "-S name" "-S used" "-S quota" \
	"-t posixuser" "-t posixgroup" "-t all"

if check_version "5.11" ; then
	set -A params -- "${params[@]}" "-i" "-t smbuser" "-t smbgroup"
fi

typeset snap_fs=$QFS@snap

log_must $ZFS set groupquota@$QGROUP=500m $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 $MKFILE 50m $QFILE

$SYNC

log_must $ZFS snapshot $snap_fs

for param in "${params[@]}"; do
	log_must eval "$ZFS groupspace $param $QFS >/dev/null 2>&1"
	log_must eval "$ZFS groupspace $param $snap_fs >/dev/null 2>&1"
done

log_pass "Check the zfs groupspace with all possible parameters"
