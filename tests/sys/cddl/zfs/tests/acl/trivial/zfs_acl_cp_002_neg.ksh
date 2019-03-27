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
# ident	"@(#)zfs_acl_cp_002_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_cp_002_neg
#
# DESCRIPTION:
#	Verifies that cp will not include file attribute when the -@ flag is not
#	present.
#
# STRATEGY:
#	1. In directory A, create several files and add attribute files for them
#	2. Implement cp to files without '-@'
#	3. Verify attribute files will not include file attribute
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-01)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verifies that cp will not include file attribute when the -@ flag "\
	"is not present."
log_onexit cleanup

test_requires ZFS_ACL ZFS_XATTR

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must create_files $TESTDIR

	initfiles=$($LS -R $INI_DIR/*)
	typeset -i i=0
	while (( i < NUM_FILE )); do
		typeset f=$(getitem $i $initfiles)
		usr_exec $CP $f $TST_DIR

		testfiles=$($LS -R $TST_DIR/*)
		tf=$(getitem $i $testfiles)
		ls_attr=$($LS -@ $tf | $AWK '{print substr($1, 11, 1)}')
		if [[ $ls_attr == "@" ]]; then
			log_fail "cp of attribute should fail without " \
				"-@ or -p option"
		fi

		(( i += 1 ))
	done

	log_must cleanup
done

log_pass "'cp' won't include file attribute passed."
