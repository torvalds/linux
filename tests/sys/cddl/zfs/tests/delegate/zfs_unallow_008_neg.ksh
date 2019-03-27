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
# ident	"@(#)zfs_unallow_008_neg.ksh	1.1	07/01/09 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unallow_008_neg
#
# DESCRIPTION:
#	zfs unallow can handle invalid arguments.
#
# STRATEGY:
#	1. Set up basic test environment.
#	2. Verify zfs unallow handle invalid arguments correctly.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-30)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "zfs unallow can handle invalid arguments."
log_onexit restore_root_datasets

function neg_test
{
	log_mustnot eval "$@ > /dev/null 2>&1"	
}

set -A badopts "everyone -e"	"everyone -u $STAFF1" 	"everyone everyone" \
	"-c -l"	"-c -d"		"-c -e"		"-c -s"	"-r" \
	"-u -e"	"-s -e" 	"-s -l -d"	"-s @non-exist-set -l" \
	"-s @non-existen-set -d"	"-s @non-existen-set -e" \
	"-r -u $STAFF1 $STAFF1" 	"-u $STAFF1 -g $STAFF_GROUP" \
	"-u $STAFF1 -e" 

log_must setup_unallow_testenv

for dtst in $DATASETS ; do
	log_must $ZFS allow -c create $dtst

	typeset -i i=0
	while ((i < ${#badopts[@]})); do
		neg_test $ZFS unallow ${badopts[$i]} $dtst
		((i += 1))
	done

	neg_test user_run $STAFF1 $ZFS unallow $dtst
done

log_pass "zfs unallow can handle invalid arguments passed."
