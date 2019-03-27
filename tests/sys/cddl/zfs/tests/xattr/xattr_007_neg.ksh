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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)xattr_007_neg.ksh	1.2	08/02/27 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/xattr/xattr_common.kshlib

# $FreeBSD$

################################################################################
#
# __stc_assertion_start
#
# ID:  xattr_007_neg
#
# DESCRIPTION:
# Creating and writing xattrs on files in snapshot directories fails. Also,
# we shouldn't be able to list the xattrs of files in snapshots who didn't have
# xattrs when the snapshot was created (the xattr namespace wouldn't have been
# created yet, and snapshots are read-only) See fsattr(5) for more details.
#
# STRATEGY:
#	1. Create a file and add an xattr to it.
#	2. Create another file, but don't add an xattr to it.
#       3. Snapshot the filesystem
#	4. Verify we're unable to alter the xattr on the first file
#	5. Verify we're unable to list the xattrs on the second file
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-13)
#
# __stc_assertion_end
#
################################################################################

function cleanup {
	log_must $ZFS destroy $TESTPOOL/$TESTFS@snap
	log_must $RM $TESTDIR/myfile2.${TESTCASE_ID}
	log_must $RM $TESTDIR/myfile.${TESTCASE_ID}
	log_must $RM $TMPDIR/output.${TESTCASE_ID}
	[[ -e $TMPDIR/expected_output.${TESTCASE_ID} ]]  && log_must $RM  \
	$TMPDIR/expected_output.${TESTCASE_ID}
	
}

log_assert "create/write xattr on a snapshot fails"
log_onexit cleanup

# create a file, and an xattr on it
log_must $TOUCH $TESTDIR/myfile.${TESTCASE_ID}
create_xattr $TESTDIR/myfile.${TESTCASE_ID} passwd /etc/passwd

# create another file that doesn't have an xattr
log_must $TOUCH $TESTDIR/myfile2.${TESTCASE_ID}

# snapshot the filesystem
log_must $ZFS snapshot $TESTPOOL/$TESTFS@snap

# we shouldn't be able to alter the first file's xattr
log_mustnot eval " $RUNAT $TESTDIR/$(get_snapdir_name)/snap/myfile.${TESTCASE_ID} \
	$CP /etc/passwd .  >$TMPDIR/output.${TESTCASE_ID}  2>&1"
log_must $GREP  -i  Read-only  $TMPDIR/output.${TESTCASE_ID}  

if check_version "5.10"
then
	# we shouldn't be able to list xattrs at all on the second file
	log_mustnot eval " $RUNAT $TESTDIR/$(get_snapdir_name)/snap/myfile2.${TESTCASE_ID} \
	 $LS  >$TMPDIR/output.${TESTCASE_ID}  2>&1"
	log_must $GREP  -i  Read-only  $TMPDIR/output.${TESTCASE_ID}  
else
	log_must eval "$RUNAT $TESTDIR/$(get_snapdir_name)/snap/myfile2.${TESTCASE_ID}  \
	$LS >$TMPDIR/output.${TESTCASE_ID}  2>&1"
	create_expected_output  $TMPDIR/expected_output.${TESTCASE_ID}   SUNWattr_ro  \
	 SUNWattr_rw
	log_must $DIFF $TMPDIR/output.${TESTCASE_ID} $TMPDIR/expected_output.${TESTCASE_ID}
fi
log_pass "create/write xattr on a snapshot fails"
