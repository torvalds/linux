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
# ident	"@(#)zfs_rename_003_pos.ksh	1.3	07/02/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_003_pos
#
# DESCRIPTION:
#	'zfs rename' can address the abbreviated snapshot name. 
#
# STRATEGY:
#	1. Create pool, fs and snap.
#	2. Verify 'zfs rename' support the abbreviated snapshot name.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-18)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	datasetexists $snap && log_must $ZFS destroy $snap 
}

log_assert "'zfs rename' can address the abbreviated snapshot name."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS; snap=$fs@snap
set -A newname "$fs@new-snap" "@new-snap" "new-snap"

log_must $ZFS snapshot $snap
log_must datasetexists $snap

typeset -i i=0
while ((i < ${#newname[*]} )); do
        log_must $ZFS rename $snap ${newname[$i]}
	log_must datasetexists ${snap%%@*}@${newname[$i]##*@}
	log_must $ZFS rename ${snap%%@*}@${newname[$i]##*@} $snap

	((i += 1))
done

log_pass "'zfs rename' address the abbreviated snapshot name passed."
