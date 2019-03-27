#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)quota_005_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: quota_005_pos
#
# DESCRIPTION:
#
# Verify that quota doesn't inherit its value from parent.
#
# STRATEGY:
# 1) Set quota for parents
# 2) Create a filesystem tree
# 3) Verify that the 'quota' for descendent doesnot inherit the value.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-17)
#
# __stc_assertion_end
#
###############################################################################  

verify_runnable "both"

function cleanup
{
	datasetexists $fs_child && \
		log_must $ZFS destroy $fs_child

	log_must $ZFS set quota=none $fs
}

log_onexit cleanup

log_assert "Verify that quota does not inherit its value from parent."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
fs_child=$TESTPOOL/$TESTFS/$TESTFS

typeset -l space_avail=$(get_prop available $fs)
typeset -l quotasize=$space_avail
((quotasize = quotasize * 2 ))
log_must $ZFS list
log_must $ZFS set quota=$quotasize $fs 

log_must $ZFS create $fs_child
typeset -l quota_space=$(get_prop quota $fs_child)
[[ $quota_space == $quotasize ]] && \
	log_fail "The quota of child dataset inherits its value from parent."

log_pass "quota doesnot inherit its value from parent as expected."
