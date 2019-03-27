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
# ident	"@(#)atime_002_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/atime/atime_common.kshlib

################################################################################
#
# __stc_assertion_start 
#
# ID: atime_002_neg
# 
# DESCRIPTION:
# When atime=off, verify the access time for files is not updated when read. 
# It is available to pool, fs snapshot and clone.
#
# STRATEGY:
# 1. Create pool, fs. 
# 2. Create '$TESTFILE' for fs.
# 3. Create snapshot and clone.
# 4. Setting atime=off on dataset and read '$TESTFILE'.
# 5. Verify the access time is not updated.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-11)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Setting atime=off, the access time for files will not be updated \
	when read."
log_onexit cleanup

#
# Create $TESTFILE, snapshot and clone.
#
setup_snap_clone

for dst in $TESTPOOL/$TESTFS $TESTPOOL/$TESTCLONE $TESTPOOL/$TESTFS@$TESTSNAP
do
	typeset mtpt=$(get_prop mountpoint $dst)

	if [[ $dst == $TESTPOOL/$TESTFS@$TESTSNAP ]]; then
		mtpt=$(snapshot_mountpoint $dst)
	else
		log_must $ZFS set atime=off $dst
	fi

	log_mustnot check_atime_updated $mtpt/$TESTFILE
done

log_pass "Verify the property atime=off passed."
