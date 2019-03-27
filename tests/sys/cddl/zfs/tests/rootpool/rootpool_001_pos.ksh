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
# ident	"@(#)rootpool_001_pos.ksh	1.1	08/05/14 SMI"
#
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  rootpool_001_pos
#
# DESCRIPTION:
# 
# rootpool's bootfs property must be equal to <rootfs>
#
# STRATEGY:
# 1) check if the system is zfsroot or not.
# 2) get the rootpool and rootfs if it's zfs root
# 3) check the rootpool's bootfs value
# 4) chek if the boofs equal to rootfs
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-01-21)
#
# __stc_assertion_end
#
################################################################################
verify_runnable "global"
log_assert "rootpool's bootfs property must be equal to <rootfs>"

typeset rootfs=$(get_rootfs)
typeset rootpool=$(get_rootpool)
typeset bootfs=$(get_pool_prop bootfs $rootpool)

if  [[ $bootfs != $rootfs ]]; then
	log_fail "rootfs is not same as bootfs."
fi

log_pass "rootpool's bootfs property equal to rootfs."

