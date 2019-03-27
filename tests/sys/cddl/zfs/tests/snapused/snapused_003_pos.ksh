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
# ident	"@(#)snapused_003_pos.ksh	1.1	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/snapused/snapused.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: snapused_003_pos
#
# DESCRIPTION:
#	Verify usedbydataset is correct.
#
# STRATEGY:
#	1. Create a filesystem.
#	2. Make file in the filesystem.
#	3. Snapshot it.
#	4. Clone it and make file in the cloned filesystem.
#	5. Check usedbydataset is correct.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-04-28)
#
# __stc_assertion_end
#
################################################################################

if ! usedby_supported ; then
	log_unsupported "snapused property is not supported."
fi

verify_runnable "both"

function cleanup
{
	log_must $ZFS destroy -rR $USEDTEST
}

log_assert "Verify usedbydataset is correct."
log_onexit cleanup

log_must $ZFS create $USEDTEST
check_usedbydataset $USEDTEST

typeset -i i=0
typeset -i r_size=0
mntpnt=$(get_prop mountpoint $USEDTEST)
while (( i < 5 )); do
	((r_size=(i+1)*16))

	log_must $MKFILE 16M $mntpnt/file$i
	log_must $MKFILE "$r_size"M $mntpnt/file_var$i
	log_must $ZFS snapshot -r $USEDTEST@snap$i

	log_must $ZFS clone $USEDTEST@snap$i $USEDTEST/cln$i
	log_must $ZFS set is:cloned=yes $USEDTEST/cln$i	

	mntpnt_cln=$(get_prop mountpoint $USEDTEST/cln$i)
	log_must $MKFILE 16M $mntpnt_cln/file_cln$i
	log_must $MKFILE "$r_size"M $mntpnt_cln/file_cln_var$i

	check_usedbydataset $USEDTEST

        ((i = i + 1))
done

log_pass "Verify usedbydataset is correct."

