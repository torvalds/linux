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
# ident	"@(#)zpool_upgrade_009_neg.ksh	1.4	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_upgrade/zpool_upgrade.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_upgrade_009_neg
#
# DESCRIPTION:
#
# Zpool upgrade -V shouldn't be able to upgrade a pool to an unknown version
#
# STRATEGY:
# 1. Take an existing pool
# 2. Attempt to upgrade it to an unknown version
# 3. Verify that the upgrade failed, and the pool version was still the original
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-09-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	destroy_upgraded_pool $config
}

log_assert \
"Zpool upgrade -V shouldn't be able to upgrade a pool to an unknown version"

$ZPOOL upgrade --help 2>&1 | $GREP "V version" > /dev/null
if [ $? -ne 0 ]
then
        log_unsupported "Zpool upgrade -V not supported on this release."
fi
log_onexit cleanup

# Create a version 2 pool
typeset -i config=2
create_old_pool $config
pool=$($ENV| $GREP "ZPOOL_VERSION_${config}_NAME" | $AWK -F= '{print $2}')

# Attempt to upgrade it
log_mustnot $ZPOOL upgrade -V 999 $pool
log_mustnot $ZPOOL upgrade -V 999

# Verify we're still on the old version
check_poolversion $pool $config
destroy_upgraded_pool $config

log_pass \
 "Zpool upgrade -V shouldn't be able to upgrade a pool to an unknown version"

