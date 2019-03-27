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
# ident	"@(#)zfs_upgrade_006_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_upgrade/zfs_upgrade.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_upgrade_006_neg
#
# DESCRIPTION:
# Verify that invalid upgrade parameters and options are caught.
#
# STRATEGY:
# 1. Create a ZFS file system.
# 2. For each option in the list, try 'zfs upgrade'.
# 3. Verify that the operation fails as expected.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

set -A args "" "-?" "-A" "-R" "-b" "-c" "-d" "--invalid" \
    "-V" "-V $TESTPOOL/$TESTFS" "-V $TESTPOOL $TESTPOOL/$TESTFS"

log_assert "Badly-formed 'zfs upgrade' should return an error."

typeset -i i=1
while (( i < ${#args[*]} )); do
	log_mustnot $ZFS upgrade ${args[i]}
	((i = i + 1))
done

log_pass "Badly-formed 'zfs upgrade' fail as expected."
