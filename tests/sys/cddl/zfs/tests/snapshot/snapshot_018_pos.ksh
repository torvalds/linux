#! /usr/local/bin/ksh93 -p
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
# Copyright 2012 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_018_pos
#
# DESCRIPTION:
#	Snapshot directory supports ACL operations
#
# STRATEGY:
#	1. Create a dataset
#	2. Set the snapdir property to visible
#	3. Use getconf to verify that acls are supported
#	4. Use getfacl to verify that the acl can be read
#	5. Verify that the acl is correct
#	6. Verify that ls doesn't print any errors because acl_get_link fails
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2013-01-03)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify that the snapshot directory supports ACL operations"

log_must dataset_setprop $TESTPOOL/$TESTFS "snapdir" "visible"

function verify_dir # directory
{
	typeset DIR=$1
	# 3. Verify that ACLs are supported by the ctldir
	log_must $GETCONF TRUSTEDBSD_ACL_EXTENDED $DIR
	# 4. Verify that we can read ACLs
	log_must $GETFACL $DIR
	# 5. Verify that the ACLs are correct
	typeset PROG_DIR=$STF_SUITE/tests/snapshot
	typeset SUM=$($SHA1 -q $PROG_DIR/ctldir_acl.txt)
	if [[ $( $GETFACL -q $DIR | $SHA1 -q ) != $SUM ]]; then
		log_fail "ACL is incorrect"
	fi

}

typeset -a dirlist
dirlist=( "$TESTDIR/.zfs" "$TESTDIR/.zfs/snapshot" ) 
ctldir=".zfs"
for d in ${dirlist[@]}; do
	verify_dir $d
done

# 6. Check for errors with ls
LS_STDERR=$( $LS -la $TESTDIR/$ctldir 2>&1 > /dev/null)
LS_R_STDERR=$( $LS -lar $TESTDIR/$ctldir 2>&1 > /dev/null)
if [[ -n $LS_STDERR || -n $LS_R_STDERR ]]; then
	log_fail "ls encountered errors"
fi

log_pass
