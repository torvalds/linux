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
# ident	"@(#)zfs_rollback_004_neg.ksh	1.2	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_rollback/zfs_rollback_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rollback_004_neg
#
# DESCRIPTION:
#	'zfs rollback' should fail when passing invalid options, too many
#	arguments,non-snapshot datasets or missing datasets	
#
# STRATEGY:
#	1. Create an array of invalid options 
#	2. Execute 'zfs rollback' with invalid options, too many arguments 
#	   or missing datasets
#	3. Verify 'zfs rollback' return with errors
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset ds

	for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL; do
		if snapexists ${ds}@$TESTSNAP; then
			log_must $ZFS destroy ${ds}@$TESTSNAP
		fi
	done
}

log_assert "'zfs rollback' should fail with bad options,too many arguments," \
	"non-snapshot datasets or missing datasets."	
log_onexit cleanup

set -A badopts "r" "R" "f" "-F" "-rF" "-RF" "-fF" "-?" "-*" "-blah" "-1" "-2" 

for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL; do
	log_must $ZFS snapshot ${ds}@$TESTSNAP
done

for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL; do
	for opt in "" "-r" "-R" "-f" "-rR" "-rf" "-rRf"; do
		log_mustnot eval "$ZFS rollback $opt $ds >/dev/null 2>&1"
		log_mustnot eval "$ZFS rollback $opt ${ds}@$TESTSNAP \
			${ds}@$TESTSNAP >/dev/null 2>&1"
		log_mustnot eval "$ZFS rollback $opt >/dev/null 2>&1"
		# zfs rollback should fail with non-existen snapshot
		log_mustnot eval "$ZFS rollback $opt ${ds}@nosnap >/dev/null 2>&1"
	done

	for badopt in ${badopts[@]}; do
		log_mustnot eval "$ZFS rollback $badopt ${ds}@$TESTSNAP \
				>/dev/null 2>&1"
	done
done

log_pass "'zfs rollback' fails as expected with illegal arguments." 
