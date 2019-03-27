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
# ident	"@(#)zpool_upgrade_006_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_upgrade_006_neg
#
# DESCRIPTION:
# Attempting to upgrade a non-existent pool will return an error
#
# STRATEGY:
# 1. Verify a pool doesn't exist, then try to upgrade it
# 2. Verify a 0 exit status
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

log_assert "Attempting to upgrade a non-existent pool will return an error"
NO_POOL=notapool
FOUND=""

while [ -z "$FOUND" ]
do
   $ZPOOL list $NO_POOL 2>&1 > /dev/null
   if [ $? -ne 0 ]
   then
      FOUND="true"
      log_mustnot $ZPOOL upgrade $NO_POOL
   else
      NO_POOL="${NO_POOL}x"
   fi
done

log_pass "Attempting to upgrade a non-existent pool will return an error"
