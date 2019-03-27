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
# ident	"@(#)zfs_clone_009_neg.ksh	1.1	09/01/13 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/properties.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_clone_009_neg
#
# DESCRIPTION:
# 'zfs clone -o <volume>' fails with badly formed arguments,including:
#       *Same property set multiple times via '-o property=value' 
#       *Filesystems's property set on volume
#
# STRATEGY:
# 1. Create an array of badly formed arguments
# 2. For each argument, execute 'zfs clone -o <volume>'
# 3. Verify an error is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-12-16)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if snapexists $SNAPFS1 ; then
		log_must $ZFS destroy -Rf $SNAPFS1
	fi
}

if ! $(check_opt_support "clone" "-o") ; then
	log_unsupported "'zfs clone -o' unsupported."
fi

log_onexit cleanup

log_assert "Verify 'zfs clone -o <volume>' fails with bad <volume> argument."

log_must $ZFS snapshot $SNAPFS1

typeset -i i=0
while (( $i < ${#RW_VOL_PROP[*]} )); do
       	log_mustnot $ZFS clone -o ${RW_VOL_PROP[i]} -o ${RW_VOL_PROP[i]} \
		$SNAPFS1 $TESTPOOL/$TESTCLONE
       	log_mustnot $ZFS clone -p -o ${RW_VOL_PROP[i]} -o ${RW_VOL_PROP[i]} \
		$SNAPFS1 $TESTPOOL/$TESTCLONE
	((i = i + 1))
done

i=0
while (( $i < ${#FS_ONLY_PROP[*]} )); do
       	log_mustnot $ZFS clone  -o ${FS_ONLY_PROP[i]} \
		$SNAPFS1 $TESTPOOL/$TESTCLONE
       	log_mustnot $ZFS clone -p -o ${FS_ONLY_PROP[i]} \
		$SNAPFS1 $TESTPOOL/$TESTCLONE
	((i = i + 1))
done

log_pass "Verify 'zfs clone -o <volume>' fails with bad <volume> argument."
