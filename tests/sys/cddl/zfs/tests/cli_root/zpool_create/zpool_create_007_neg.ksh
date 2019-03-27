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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_create_007_neg.ksh	1.5	08/11/03 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_007_neg
#
# DESCRIPTION:
# 'zpool create' should return an error with badly formed parameters.
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute 'zpool create'
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

verify_runnable "global"

if [[ -n $DISK ]]; then
	disk=$DISK
else
	disk=$DISK0
fi

set -A args  "" "-?" "-n" "-f" "-nf" "-fn" "-f -n" "--f" "-e" "-s" \
	"-m" "-R" "-m -R" "-Rm" "-mR" "-m $TESTDIR $TESTPOOL" \
	"-R $TESTDIR $TESTPOOL" "-m nodir $TESTPOOL $disk" \
	"-R nodir $TESTPOOL $disk" "-m nodir -R nodir $TESTPOOL $disk" \
	"-R nodir -m nodir $TESTPOOL $disk" "-R $TESTDIR -m nodir $TESTPOOL $disk" \
	"-R nodir -m $TESTDIR $TESTPOOL $disk" \
	"-blah" "$TESTPOOL" "$TESTPOOL blah" "$TESTPOOL c?t0d0" \
	"$TESTPOOL c0txd0" "$TESTPOOL c0t0dx" "$TESTPOOL cxtxdx" \
	"$TESTPOOL mirror" "$TESTPOOL raidz" "$TESTPOOL mirror raidz" \
	"$TESTPOOL raidz1" "$TESTPOOL mirror raidz1" \
	"$TESTPOOL mirror c?t?d?" "$TESTPOOL mirror $disk c0t1d?" \
	"$TESTPOOL RAIDZ ${disk}p1 ${disk}p2" \
	"$TESTPOOL ${disk}p1 log ${disk}p2 \
	log ${disk}p3" \
	"$TESTPOOL ${disk}p1 spare ${disk}p2 \
	spare ${disk}p3" \
	"$TESTPOOL RAIDZ1 ${disk}p1 ${disk}p2" \
	"$TESTPOOL MIRROR $disk" "$TESTPOOL raidz $disk" \
	"$TESTPOOL raidz1 $disk" \
	"1tank $disk" "1234 $disk" "?tank $disk" \
	"tan%k $disk" "ta@# $disk" "tan+k $disk" \
	"$BYND_MAX_NAME $disk" 

log_assert "'zpool create' should return an error with badly-formed parameters."
log_onexit default_cleanup_noexit

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot $ZPOOL create ${args[i]}
	((i = i + 1))
done

log_pass "'zpool create' with badly formed parameters failed as expected."
