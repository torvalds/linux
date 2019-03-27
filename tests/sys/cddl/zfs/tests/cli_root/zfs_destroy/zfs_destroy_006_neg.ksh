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
# ident	"@(#)zfs_destroy_006_neg.ksh	1.3	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_destroy_006_neg
#
# DESCRIPTION:
# 'zfs destroy' should return an error with badly formed parameters,
# including null destroyed object parameter, invalid options excluding
# '-r' and '-f', non-existent datasets.
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute 'zfs destroy'
# 3. Verify an error is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"


set -A args "" "-r" "-f" "-rf" "-fr" "$TESTPOOL" "-f $TESTPOOL" \
	"-? $TESTPOOL/$TESTFS" "$TESTPOOL/blah"\
        "-r $TESTPOOL/blah" "-f $TESTPOOL/blah" "-rf $TESTPOOL/blah" \
	"-fr $TESTPOOL/blah" "-$ $TESTPOOL/$TESTFS" "-5 $TESTPOOL/$TESTFS" \
	"-rfgh $TESTPOOL/$TESTFS" "-rghf $TESTPOOL/$TESTFS" \
	"$TESTPOOL/$TESTFS@blah" "/$TESTPOOL/$TESTFS" "-f /$TESTPOOL/$TESTFS" \
	"-rf /$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTFS $TESTPOOL/$TESTFS" \
	"-rRf $TESTPOOL/$TESTFS $TESTPOOL/$TESTFS"

log_assert "'zfs destroy' should return an error with badly-formed parameters."

typeset -i i=0
while (( $i < ${#args[*]} )); do
	log_mustnot $ZFS destroy ${args[i]}
	((i = i + 1))
done

log_pass "'zfs destroy' badly formed parameters fail as expected."
