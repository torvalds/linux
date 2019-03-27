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
# ident	"@(#)zfs_acl_ls_002_neg.ksh	1.3	07/07/31 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_ls_002_neg
#
# DESCRIPTION:
#	Verifies that ls doesn't display @ in the file permissions using ls -@
#	for files without attribute.
#
# STRATEGY:
#	1. Create files with attribute files in directory A.
#	2. Removed all attribute files.
#	3. Verify 'ls -l' can't display @ in file permission.
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

log_assert  "Verifies that ls doesn't display @ in the file " \
	"permissions using ls -@ for files without attribute."
log_onexit cleanup

test_requires RUNAT ZFS_ACL ZFS_XATTR

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must create_files $TESTDIR

	initfiles=$($LS -R $INI_DIR/*)
	typeset -i i=0
	while (( i < NUM_FILE )); do
		f=$(getitem $i $initfiles)
		log_must usr_exec $RUNAT $f $RM attribute*

		ls_attr=$(usr_exec $LS -l $f | $AWK '{print substr($1, 11, 1)}')
		if [[ $ls_attr == "@" ]]; then
			log_fail "ls with attribute shouldn't success."
		fi

		(( i += 1 ))
	done

	log_must cleanup
done

log_pass "ls doesn't display @ in file permissions passed."
