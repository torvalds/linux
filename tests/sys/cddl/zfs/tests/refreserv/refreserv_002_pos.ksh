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
# ident	"@(#)refreserv_002_pos.ksh	1.3	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: refreserv_002_pos
#
# DESCRIPTION:
#	Setting full size as refreservation, verify no snapshot can be created.
#
# STRATEGY:
#	1. Setting full size as refreservation on pool
#	2. Verify no snapshot can be created on this pool
#	3. Setting full size as refreservation on filesystem
#	4. Verify no snapshot can be created on it and its subfs
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-11-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if is_global_zone ; then
		log_must $ZFS set refreservation=none $TESTPOOL

		if datasetexists $TESTPOOL@snap ; then
			log_must $ZFS destroy -f $TESTPOOL@snap
		fi
	fi
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

# This function iteratively increases refreserv to its highest possible
# value. Simply setting refreserv == quota can allow enough writes to
# complete that the test fails.
function max_refreserv
{
	typeset ds=$1
	typeset -i incsize=131072
	typeset -i rr=$(get_prop available $ds)

	log_must $ZFS set refreserv=$rr $ds
	while :; do
		$ZFS set refreserv=$((rr + incsize)) $ds >/dev/null 2>&1
		if [[ $? == 0 ]]; then
			((rr += incsize))
			continue
		else
			((incsize /= 2))
			((incsize == 0)) && break
		fi
	done
}


log_assert "Setting full size as refreservation, verify no snapshot " \
	"can be created."
log_onexit cleanup

log_must $ZFS create $TESTPOOL/$TESTFS/subfs

typeset datasets
if is_global_zone; then
	datasets="$TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTFS/subfs"
else
	datasets="$TESTPOOL/$TESTFS $TESTPOOL/$TESTFS/subfs"
fi

for ds in $datasets; do
	#
	# Verify refreservation on dataset
	#
	log_must $ZFS set quota=25M $ds
	max_refreserv $ds
	log_mustnot $ZFS snapshot $ds@snap
	if datasetexists $ds@snap ; then
		log_fail "ERROR: $ds@snap should not exists."
	fi

	log_must $ZFS set quota=none $ds
	log_must $ZFS set refreservation=none $ds
done

log_pass "Setting full size as refreservation, verify no snapshot " \
	"can be created."
