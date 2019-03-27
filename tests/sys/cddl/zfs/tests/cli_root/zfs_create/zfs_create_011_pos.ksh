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
# ident	"@(#)zfs_create_011_pos.ksh	1.2	09/01/13 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_create_011_pos
#
# DESCRIPTION:
# 'zfs create -p'  should work as expecteed
#
# STRATEGY:
# 1. To create $newdataset with -p option, first make sure the upper level
#    of $newdataset does not exist
# 2. Make sure without -p option, 'zfs create' will fail
# 3. Create $newdataset with -p option, verify it is created
# 4. Run 'zfs create -p $newdataset' again, the exit code should be zero
#    even $newdataset exists
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-05)
#
# __stc_assertion_end
#
################################################################################

if ! $(check_opt_support "create" "-p") ; then
	log_unsupported "'zfs create -p' option is not supported yet."
fi

verify_runnable "both"

function cleanup
{
	if datasetexists $TESTPOOL/$TESTFS1 ; then
		log_must $ZFS destroy -rf $TESTPOOL/$TESTFS1
	fi
}

log_onexit cleanup

typeset newdataset1="$TESTPOOL/$TESTFS1/$TESTFS/$TESTFS1" 
typeset newdataset2="$TESTPOOL/$TESTFS1/$TESTFS/$TESTVOL1" 

log_assert "'zfs create -p' works as expected."

log_must verify_opt_p_ops "create" "fs" $newdataset1

# verify volume creation
if is_global_zone; then
	log_must verify_opt_p_ops "create" "vol" $newdataset2
fi
	
log_pass "'zfs create -p' works as expected."
