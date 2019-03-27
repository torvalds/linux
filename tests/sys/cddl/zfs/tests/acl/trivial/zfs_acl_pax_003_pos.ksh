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
# ident	"@(#)zfs_acl_pax_003_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_pax_003_pos
#
# DESCRIPTION:
#	Verify directories which include attribute in pax archive and restore
#	with cpio should succeed.
#
# STRATEGY:
#	1. Create several files in directory A.
#	2. Enter into directory A and record all directory cksum.
#	3. pax all the files to directory B.
#	4. Then cpio the pax file to directory C.
#	5. Record all the files cksum in derectory C.
#	6. Verify the two records should be identical.
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

log_assert "Verify include attribute in pax archive and restore with cpio " \
	"should succeed."
log_onexit cleanup

test_requires ZFS_ACL ZFS_XATTR

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	[[ ! -d $INI_DIR ]] && log_must usr_exec $MKDIR -m 777 -p $INI_DIR
	log_must usr_exec $MKTREE -b $INI_DIR -l 6 -d 2 -f 2

	initout=$TMP_DIR/initout.${TESTCASE_ID}
	paxout=$TMP_DIR/files.cpio
	cd $INI_DIR
	log_must eval "record_cksum $INI_DIR $initout > /dev/null 2>&1"
	log_must eval "usr_exec $PAX -w -x cpio -@ -f $paxout * >/dev/null 2>&1"

	#
	# Enter into test directory and cpio $TMP_DIR/files.pax to current
	# directory. Record all directory information and compare with initial
	# directory record.
	#
	[[ ! -d $TST_DIR ]] && log_must usr_exec $MKDIR -m 777 $TST_DIR
	testout=$TMP_DIR/testout.${TESTCASE_ID}
	cd $TST_DIR
	log_must eval "usr_exec $CPIO -ivd@ < $paxout" > /dev/null 2>&1
	log_must eval "record_cksum $TST_DIR $testout > /dev/null 2>&1"

	log_must usr_exec $DIFF $initout $testout

	log_must cleanup
done

log_pass "Directories pax archive and restore with cpio passed."
