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
# ident	"@(#)zpool_create_001_neg.ksh	1.4	09/01/12 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_001_neg
#
# DESCRIPTION:
# Verify that 'zpool create' fails as a non-root user.
#
# STRATEGY:
# 1. Create an array of options.
# 2. Execute each element of the array.
# 3. Verify that an error is returned.
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

ADD_DISK="${DISKS%% }"
ADD_DISK="${ADD_DISK##* }"

[[ -z $ADD_DISK ]] && \
        log_fail "No spare disks available."

set -A args "create $TESTPOOL $ADD_DISK" "create -f $TESTPOOL $ADD_DISK"

log_assert "zpool create [-fn] pool_name vdev"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot $ZPOOL ${args[i]}
	((i = i + 1))
done

log_pass "The sub-command 'create' and its options fail as non-root."
