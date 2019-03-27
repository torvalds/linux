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
# ident	"@(#)zfs_allow_012_neg.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_allow_012_neg
#
# DESCRIPTION:
#	Scan all permissions one by one to verify privileged user 
#	can not use permissions properly when delegation property is set off
#
# STRATEGY:
#	1. Delegate all the permission one by one to user on dataset.
#	2. Verify privileged user can not use permissions properly when
#	delegation property is off
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-19)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	log_must $ZPOOL set delegation=on $TESTPOOL
	log_must restore_root_datasets
}

log_assert "Verify privileged user can not use permissions properly when " \
	"delegation property is set off"
log_onexit cleanup


set -A perms	create snapshot mount send allow quota reservation \
	    	recordsize mountpoint checksum compression canmount atime \
		devices exec volsize setuid readonly snapdir userprop \
		aclmode aclinherit rollback clone rename promote \
		zoned shareiscsi xattr receive destroy sharenfs share

log_must $ZPOOL set delegation=off $TESTPOOL

for dtst in $DATASETS; do
	typeset -i i=0
	while (( i < ${#perms[@]} )); do

		log_must $ZFS allow $STAFF1 ${perms[$i]} $dtst
		log_must verify_noperm $dtst ${perms[$i]} $STAFF1 

		log_must restore_root_datasets
		((i += 1))
	done
done

log_pass "Verify privileged user can not use permissions properly when " \
	"delegation property is set off"
