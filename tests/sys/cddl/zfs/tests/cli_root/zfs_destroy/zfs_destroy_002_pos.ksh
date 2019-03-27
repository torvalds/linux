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
# ident	"@(#)zfs_destroy_002_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_destroy_002_pos
#
# DESCRIPTION: 
#	'zfs destroy <filesystem|volume|snapshot>' can successfully destroy 
#	the specified dataset which has no active dependents.
#
# STRATEGY:
#	1. Create a filesystem,volume and snapshot in the storage pool
#	2. Destroy the filesystem,volume and snapshot
#	3. Verify the datasets are destroyed successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-07)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset -i i=0
	while (( $i < ${#data_objs[*]} )); do
		datasetexists "${data_objs[i]}" && \
			$ZFS destroy -rf ${data_objs[i]}
		((i = i + 1))
	done
}

log_assert "Verify 'zfs destroy' can destroy the specified datasets without active" \
	"dependents."
log_onexit cleanup

if is_global_zone ; then
	set -A data_objs "$TESTPOOL/$TESTFS@$TESTSNAP" "$TESTPOOL/$TESTFS1" \
		"$TESTPOOL/$TESTVOL" "$TESTPOOL/$TESTVOL1"
else
	set -A data_objs "$TESTPOOL/$TESTFS@$TESTSNAP" "$TESTPOOL/$TESTFS1" 
fi

log_must $ZFS create $TESTPOOL/$TESTFS1
log_must $ZFS snapshot $TESTPOOL/$TESTFS@$TESTSNAP

if is_global_zone ; then
	log_must $ZFS create -V $VOLSIZE $TESTPOOL/$TESTVOL

	# Max volume size is 1TB on 32-bit systems
	[[ `$UNAME -p` == "i386" ]] && \
		BIGVOLSIZE=1Tb
	[[ `$UNAME -p` == "arm" ]] && \
		BIGVOLSIZE=1Tb
	[[ `$UNAME -p` == "mips" ]] && \
		BIGVOLSIZE=1Tb
	[[ `$UNAME -p` == "powerpc" ]] && \
		BIGVOLSIZE=1Tb
	log_must $ZFS create -sV $BIGVOLSIZE $TESTPOOL/$TESTVOL1
fi

typeset -i i=0
while (( $i < ${#data_objs[*]} )); do
	datasetexists ${data_objs[i]} || \
		log_fail "Create <filesystem>|<volume>|<snapshot> fail."
	((i = i + 1))
done

i=0
while (( $i < ${#data_objs[*]} )); do
	log_must $ZFS destroy ${data_objs[i]}
	datasetexists ${data_objs[i]} && \
		log_fail "'zfs destroy <filesystem>|<volume>|<snapshot>' fail."
	((i = i + 1))
done

log_pass "'zfs destroy <filesystem>|<volume>|<snapshot>' works as expected."
