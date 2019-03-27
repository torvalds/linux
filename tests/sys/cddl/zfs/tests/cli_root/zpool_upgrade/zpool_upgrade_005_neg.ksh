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
# ident	"@(#)zpool_upgrade_005_neg.ksh	1.3	07/10/09 SMI"
#
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_upgrade_005_neg
#
# DESCRIPTION:
# Variations of upgrade -v print usage message, return with non-zero status
#
# STRATEGY:
# 1. Execute the command with several invalid options
# 2. Verify a 0 exit status for each
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

verify_runnable "global"

set -A args "/tmp" "-?" "-va" "-v fakepool" "-a fakepool"

log_assert "Variations of upgrade -v print usage message, \
 return with non-zero status"

typeset -i i=0

while [[ $i -lt ${#args[*]} ]]; do

        log_mustnot $ZPOOL upgrade ${args[$i]} > /dev/null

        (( i = i + 1 ))
done

log_pass "Variations of upgrade -v print usage message, \
 return with non-zero status"
