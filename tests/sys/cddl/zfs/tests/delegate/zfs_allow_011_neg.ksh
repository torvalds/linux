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
# ident	"@(#)zfs_allow_011_neg.ksh	1.2	07/07/31 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_allow_011_neg
#
# DESCRIPTION:
#	Verify zpool subcmds and system readonly properties can't be delegated.
#
# STRATEGY:
#	1. Loop all the zpool subcmds and readonly properties, except permission
#	   'create' & 'destroy'.
#	2. Verify those subcmd or properties can't be delegated.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify zpool subcmds and system readonly properties can't be " \
	"delegated."
log_onexit restore_root_datasets

set -A invalid_perms	\
	add		remove		list		iostat		\
	status		offline		online 		clear		\
	attach		detach		replace		scrub		\
	export		import		upgrade				\
	type		creation	used		available	\
	referenced	compressratio	mounted	

for dtst in $DATASETS ; do
	typeset -i i=0

	while ((i < ${#invalid_perms[@]})); do
		log_mustnot $ZFS allow $STAFF1 ${invalid_perms[$i]} $dtst

		((i += 1))
	done
done

log_pass "Verify zpool subcmds and system readonly properties passed."
