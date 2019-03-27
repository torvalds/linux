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
# ident	"@(#)iscsi_002_neg.ksh	1.2	07/05/25 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
# __stc_assertion_start
#
# ID: iscsi_002_neg
#
# DESCRIPTION:
#	Verify file systems and snapshots can not be shared via iSCSI
#
# STRATEGY:
#	1) Turn on shareiscsi property directly on the filesystem
#	2) Check if the target is created or not
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-21)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if [[ "off" != $(get_prop shareiscsi $TESTPOOL/$TESTFS) ]]; then
		log_must $ZFS set shareiscsi=off $TESTPOOL/$TESTFS
	fi
	datasetexists $TESTPOOL/$TESTFS@snap && \
		log_must $ZFS destroy -f $TESTPOOL/$TESTFS@snap
}

log_onexit cleanup

log_assert "Verify file systems and snapshots can not be shared via iSCSI."

if [[ "off" != $(get_prop shareiscsi $TESTPOOL/$TESTFS) ]]; then
	log_fail "The default value of shareiscsi should be off."
fi

# Check shareiscsi property directly on filesystem at first
log_must $ZFS set shareiscsi=on $TESTPOOL/$TESTFS
if is_iscsi_target $TESTPOOL/$TESTFS ; then
	log_fail "shareiscsi property on filesystem makes an iSCSI target \
		unexpectedly."
fi

log_must $ZFS snapshot $TESTPOOL/$TESTFS@snap
if is_iscsi_target $TESTPOOL/$TESTFS@snap ; then
	log_fail "shareiscsi property on snapshot makes an iSCSI target \
		unexpectedly."
fi

log_pass "Verify file systems and snapshots can not be shared via iSCSI."
