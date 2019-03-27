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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfs_create_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_create_001_pos
#
# DESCRIPTION:
# 'zfs create <filesystem>' can create a ZFS filesystem in the namespace.
#
# STRATEGY:
# 1. Create a ZFS filesystem in the storage pool
# 2. Verify the filesystem created successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"


function cleanup
{
	typeset -i i=0
	while (( $i < ${#datasets[*]} )); do
		datasetexists ${datasets[$i]} && \
			log_must $ZFS destroy -f ${datasets[$i]}
		((i = i + 1))
	done
}

log_onexit cleanup

set -A datasets "$TESTPOOL/$TESTFS1" "$TESTPOOL/$LONGFSNAME" "$TESTPOOL/..." \
		"$TESTPOOL/_1234_"	

log_assert "'zfs create <filesystem>' can create a ZFS filesystem in the namespace." 

typeset -i i=0
while (( $i < ${#datasets[*]} )); do 
	log_must $ZFS create ${datasets[$i]}
	datasetexists ${datasets[$i]} || \
		log_fail "zfs create ${datasets[$i]} fail."
	((i = i + 1))
done

log_pass "'zfs create <filesystem>' works as expected."
