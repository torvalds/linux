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
# ident	"@(#)zfs_copies_001_pos.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_copies/zfs_copies.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_copies_001_pos
#
# DESCRIPTION:
# 	Verify "copies" property can be correctly set as 1,2 and 3 and different
#	filesystem can have different value of "copies" property within the same pool.
#
# STRATEGY:
#	1. Create different filesystems with copies set as 1,2,3;
#	2. Verify that the "copies" property has been set correctly
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-05-31)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset ds

	for ds in $fs1 $fs2 $vol1 $vol2; do	
		if datasetexists $ds; then
			log_must $ZFS destroy $ds
		fi
	done 
}

log_assert "Verify 'copies' property with correct arguments works or not."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
fs1=$TESTPOOL/$TESTFS1
fs2=$TESTPOOL/$TESTFS2
vol=$TESTPOOL/$TESTVOL
vol1=$TESTPOOL/$TESTVOL1
vol2=$TESTPOOL/$TESTVOL2

#
# Check the default value for copies property
#
for ds in $fs $vol; do
	cmp_prop $ds 1
done

for val in 1 2 3; do
	log_must $ZFS create -o copies=$val $fs1
	if is_global_zone; then
		log_must $ZFS create -V $VOLSIZE -o copies=$val $vol1
	else
		log_must $ZFS create -o copies=$val $vol1
	fi
	for ds in $fs1 $vol1; do
		cmp_prop $ds $val
	done

	for val2 in 3 2 1; do
		log_must $ZFS create -o copies=$val2 $fs2
		if is_global_zone; then
			log_must $ZFS create -V $VOLSIZE -o copies=$val2 $vol2
		else
			log_must $ZFS create -o copies=$val2 $vol2
		fi
		for ds in $fs2 $vol2; do
			cmp_prop $ds $val2
			log_must $ZFS destroy $ds
		done
	done
	
	for ds in $fs1 $vol1; do
		log_must $ZFS destroy $ds
	done
	
done

for val in 3 2 1; do
	for ds in $fs $vol; do
		log_must $ZFS set copies=$val $ds
		cmp_prop $ds $val
	done
done

log_pass "'copies' property with correct arguments works as expected. "
