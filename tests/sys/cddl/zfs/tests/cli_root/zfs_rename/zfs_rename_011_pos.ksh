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
# ident	"@(#)zfs_rename_011_pos.ksh	1.2	09/01/13 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_rename/zfs_rename.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_011_pos
#
# DESCRIPTION
#       'zfs rename -p' should work as expected
#
# STRATEGY:
#	1. Make sure the upper level of $newdataset does not exist
#       2. Make sure without -p option, 'zfs rename' will fail
#       3. With -p option, rename works
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-05)
#
# __stc_assertion_end
#
############################################################################### 

if ! $(check_opt_support "create" "-p") ; then
	log_unsupported "-p option is not supported yet."
fi

verify_runnable "both"

function additional_cleanup
{
	if datasetexists $TESTPOOL/notexist ; then
		log_must $ZFS destroy -Rf $TESTPOOL/notexist
	fi

	if datasetexists $TESTPOOL/$TESTFS ; then
		log_must $ZFS destroy -Rf $TESTPOOL/$TESTFS
	fi
	log_must $ZFS create $TESTPOOL/$TESTFS

	if is_global_zone ; then
		if datasetexists $TESTPOOL/$TESTVOL ; then
			log_must $ZFS destroy -Rf $TESTPOOL/$TESTVOL
		fi
		log_must $ZFS create -V $VOLSIZE $TESTPOOL/$TESTVOL
	fi
}

log_onexit additional_cleanup

log_assert "'zfs rename -p' should work as expected"

log_must verify_opt_p_ops "rename" "fs" "$TESTPOOL/$TESTFS" \
	"$TESTPOOL/notexist/new/$TESTFS1"

if is_global_zone; then
	log_must verify_opt_p_ops "rename" "vol" "$TESTPOOL/$TESTVOL" \
		"$TESTPOOL/notexist/new/$TESTVOL1"
fi

log_pass "'zfs rename -p' should work as expected"
