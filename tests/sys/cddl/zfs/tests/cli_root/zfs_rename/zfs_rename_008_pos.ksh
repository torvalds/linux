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
# ident	"@(#)zfs_rename_008_pos.ksh	1.2	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_008_pos
#
# DESCRIPTION:
#	zfs rename -r can rename snapshot recursively.
#
# STRATEGY:
#	1. Create snapshot recursively.
#	2. Rename snapshot recursively.
#	3. Verify rename -r snapshot correctly.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-03-15)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

# Check if current system support recursive rename
$ZFS rename 2>&1 | grep "rename -r" > /dev/null 2>&1
if (($? != 0)); then
	log_unsupported
fi

function cleanup
{
	typeset -i i=0
	while ((i < ${#datasets[@]})); do
		if datasetexists ${datasets[$i]}@snap ; then
			log_must $ZFS destroy ${datasets[$i]}@snap
		fi
		if datasetexists ${datasets[$i]}@snap-new ; then
			log_must $ZFS destroy ${datasets[$i]}@snap-new
		fi

		((i += 1))
	done
}

log_assert "zfs rename -r can rename snapshot recursively."
log_onexit cleanup

set -A datasets $TESTPOOL		$TESTPOOL/$TESTCTR \
	$TESTPOOL/$TESTCTR/$TESTFS1	$TESTPOOL/$TESTFS
if is_global_zone; then
	datasets[${#datasets[@]}]=$TESTPOOL/$TESTVOL
fi

log_must $ZFS snapshot -r ${TESTPOOL}@snap
typeset -i i=0
while ((i < ${#datasets[@]})); do
	log_must datasetexists ${datasets[$i]}@snap

	((i += 1))
done

log_must $ZFS rename -r ${TESTPOOL}@snap ${TESTPOOL}@snap-new
i=0
while ((i < ${#datasets[@]})); do
	log_must datasetexists ${datasets[$i]}@snap-new

	((i += 1))
done

log_must $ZFS destroy -rf ${TESTPOOL}@snap-new

log_pass "Verify zfs rename -r passed."
