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
# ident	"@(#)user_property_001_pos.ksh	1.3	07/02/06 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: user_property_001_pos
#
# DESCRIPTION:
# 	ZFS can set any valid user defined property to the non-readonly dataset.
#
# STRATEGY:
# 	1. Loop pool, fs and volume.
#	2. Combine all kind of valid characters into a valid user defined
#	   property name.
#	3. Random get a string as the value.
#	4. Verify all the valid user defined properties can be set to the
#	   dataset in #1.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-31)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "ZFS can set any valid user defined property to the non-readonly " \
	"dataset."
log_onexit cleanup_user_prop $TESTPOOL

typeset -i i=0
while ((i < 10)); do
	typeset -i len
	((len = RANDOM % 32))
	typeset user_prop=$(valid_user_property $len)
	((len = RANDOM % 512))
	typeset value=$(user_property_value $len)

	for dtst in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL; do
		log_must eval "$ZFS set $user_prop='$value' $dtst"
		log_must eval "check_user_prop $dtst $user_prop '$value'"
	done

	((i += 1))
done

log_pass "ZFS can set any valid user defined property passed."
