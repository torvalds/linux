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
# ident	"@(#)zfs_set_002_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_set_002_neg
#
# DESCRIPTION:
# 'zfs set' should fail with invalid arguments 
#
# STRATEGY:
# 1. Create an array of invalid arguments
# 1. Run zfs set with each invalid argument
# 2. Verify that zfs set returns error
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-9)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "'zfs set' fails with invalid arguments"

set -A editable_props "quota" "reservation" "reserv" "volsize" "recordsize" "recsize" \
		"mountpoint" "checksum" "compression" "compress" "atime" \
		"devices" "exec" "setuid" "readonly" "zoned" "snapdir" "aclmode" \
		"aclinherit" "canmount" "shareiscsi" "xattr" "copies" "version" 

for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@$TESTSNAP; do
	for badarg in "" "-" "-?"; do
		for prop in ${editable_props[@]}; do
			log_mustnot eval "$ZFS set $badarg $prop= $ds >/dev/null 2>&1"
		done
	done
done

log_pass "'zfs set' fails with invalid arguments as expected."
