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
# ident	"@(#)zfs_snapshot_002_neg.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_snapshot_002_neg
#
# DESCRIPTION: 
#	"zfs snapshot -r" fails with invalid arguments or scenarios.
#	The invalid scenarios may include:
#	(1) The child filesystem already has snapshot with the same name
#	(2) The child volume already has snapshot with the same name
#
# STRATEGY:
#	1. Create an array of invalid arguments
#	2. Execute 'zfs snapshot -r' with each argument in the array, 
#	3. Verify an error is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-19)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset snap
	
	for snap in $TESTPOOL/$TESTCTR/$TESTFS1@$TESTSNAP \
		$TESTPOOL/$TESTCTR/$TESTVOL@$TESTSNAP;
	do
		snapexists $snap && \
			log_must $ZFS destroy $snap
	done

	datasetexists $TESTPOOL/$TESTCTR/$TESTVOL && \
		log_must $ZFS destroy -rf $TESTPOOL/$TESTCTR/$TESTVOL

}

log_assert "'zfs snapshot -r' fails with invalid arguments or scenarios. "
log_onexit cleanup

set -A args "" \
    "$TESTPOOL/$TESTCTR@$TESTSNAP" "$TESTPOOL/$TESTCTR@blah?" \
    "$TESTPOOL/$TESTCTR@blah*" "@$TESTSNAP" "$TESTPOOL/$TESTCTR@" \
    "$TESTPOOL/$TESTFS/$TESTSNAP" "blah/blah@$TESTSNAP" \
    "$TESTPOOL/$TESTCTR@$TESTSNAP@$TESTSNAP"

# setup preparations
log_must $ZFS snapshot $TESTPOOL/$TESTCTR/$TESTFS1@$TESTSNAP

# testing
typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot $ZFS snapshot -r ${args[i]}

	((i = i + 1))
done

# Testing the invalid senario: the child volume already has an 
# identical name snapshot, zfs snapshot -r should fail when
# creating snapshot with -r for the parent 
log_must $ZFS destroy $TESTPOOL/$TESTCTR/$TESTFS1@$TESTSNAP
if is_global_zone; then
	log_must $ZFS create -V $VOLSIZE $TESTPOOL/$TESTCTR/$TESTVOL
else
	log_must $ZFS create $TESTPOOL/$TESTCTR/$TESTVOL
fi
log_must $ZFS snapshot $TESTPOOL/$TESTCTR/$TESTVOL@$TESTSNAP

log_mustnot $ZFS snapshot -r $TESTPOOL/$TESTCTR@$TESTSNAP

log_pass "'zfs snapshot -r' fails with invalid arguments or scenarios as expected." 
