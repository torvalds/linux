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
# ident	"@(#)redundancy_002_pos.ksh	1.3	07/05/25 SMI"
#

. $STF_SUITE/tests/redundancy/redundancy.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: redundancy_002_pos
#
# DESCRIPTION:
#	A raidz2 pool can withstand 2 devices are failing or missing.
#
# STRATEGY:
#	1. Create N(>3,<5) virtual disk files.
#	2. Create raidz2 pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damaged at most two of the virtual disk files.
#	6. Verify the data is correct to prove raidz2 can withstand 2 devices 
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

log_assert "Verify raidz2 pool can withstand two devices are failing."
log_onexit cleanup

for cnt in 3 4; do
	setup_test_env $TESTPOOL raidz2 $cnt

	#
	# Inject data corruption errors for raidz2 pool
	#
	for i in 1 2; do
		damage_devs $TESTPOOL $i "label"
		log_must is_data_valid $TESTPOOL
		log_must clear_errors $TESTPOOL
	done

	#
	# Inject bad devices errors for raidz2 pool
	#
	for i in 1 2; do
		damage_devs $TESTPOOL $i 
		log_must is_data_valid $TESTPOOL
		log_must recover_bad_missing_devs $TESTPOOL $i
	done

	#
	# Inject missing device errors for raidz2 pool
	#
	for i in 1 2; do
		remove_devs $TESTPOOL $i
		log_must is_data_valid $TESTPOOL
		log_must recover_bad_missing_devs $TESTPOOL $i
	done
done

log_pass "Raidz2 pool can withstand two devices are failing passed."
