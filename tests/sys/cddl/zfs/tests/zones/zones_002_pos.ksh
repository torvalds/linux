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
# ident	"@(#)zones_002_pos.ksh	1.2	07/01/09 SMI"
#

. ${STF_SUITE}/include/libtest.kshlib
. ${STF_SUITE}/tests/zones/zones_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  zones_002_pos
#
# DESCRIPTION:
#
# A zone created where the zonepath parent dir is the top level of a ZFS
# file system has a new ZFS filesystem created for it.
#
# STRATEGY:
#	1. The setup script should have created the zone.
#       2. Verify that a new ZFS filesystem has been created.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-10-11)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "A ZFS fs is created when the parent dir of zonepath is a ZFS fs."

# check to see if our zone exists:
if [ ! -d /$TESTPOOL/$ZONE ]
then
	log_fail "Zone dir in /$TESTPOOL/$ZONE not found!"
fi

if [ ! -d /$TESTPOOL/simple_dir/$ZONE2 ]
then
	log_fail "Zone dir /$TESTPOOL/simple_dir/$ZONE2 not found!"
fi

# we should have a new ZFS fs for the zone
log_must eval "$ZFS list $TESTPOOL/$ZONE > /dev/null"

# we should not have a new ZFS fs for the non-ZFS zone.
log_mustnot eval "$ZFS list $TESTPOOL/simple_dir/$ZONE2 > /dev/null"

log_pass "A ZFS fs is created when the parent dir of zonepath is a ZFS fs."
