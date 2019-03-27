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
# ident	"@(#)redundancy_003_pos.ksh	1.4	07/06/05 SMI"
#

. $STF_SUITE/tests/redundancy/redundancy.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: redundancy_003_pos
#
# DESCRIPTION:
#	A mirrored pool can withstand N-1 device are failing or missing.
#
# STRATEGY:
#	1. Create N(>2,<5) virtual disk files.
#	2. Create mirror pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damaged at most N-1 of the virtual disk files.
#	6. Verify the data are correct to prove mirror can withstand N-1 devices
#	   are failing.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-21)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Verify mirrored pool can withstand N-1 devices are failing or missing."
log_onexit cleanup

for cnt in 3 2; do
	typeset -i i=1

	setup_test_env $TESTPOOL mirror $cnt

	#
	# Inject data corruption errors for mirrored pool
	#
	while (( i < cnt )); do
		damage_devs $TESTPOOL $i "label"
		log_must is_data_valid $TESTPOOL
		log_must clear_errors $TESTPOOL
		
		(( i +=1 ))
	done

	#
	# Inject  bad devices errors for mirrored pool
	#
	i=1
	while (( i < cnt )); do
		damage_devs $TESTPOOL $i
		log_must is_data_valid $TESTPOOL
		log_must recover_bad_missing_devs $TESTPOOL $i

		(( i +=1 ))
	done

	#
	# Inject missing device errors for mirrored pool
	#
	i=1
	while (( i < cnt )); do
		remove_devs $TESTPOOL $i
		log_must is_data_valid $TESTPOOL
		log_must recover_bad_missing_devs $TESTPOOL $i

		(( i +=1 ))
	done
done

log_pass "Mirrored pool can withstand N-1 devices failing as expected."
