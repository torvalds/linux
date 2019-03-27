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
# ident	"@(#)zfs_set_property_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_set_property_001_pos
#
# DESCRIPTION:
# For each property verify that it accepts on/off/inherit.
#
# STRATEGY:
# 1. Create an array of properties.
# 2. Create an array of possible values.
# 3. For each property set to every possible value.
# 4. Verify success is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"


set -A options "on" "off" "inherit"
set -A args "compression" "checksum" "mutable" "atime"

log_assert "Verify each of the file system properties."

log_untested "Due to changing zfs ls output, test needs a re-write."

typeset -i i=0
typeset -i j=0

while [[ $i -lt ${#args[*]} ]]; do
	j=0
	while [[ $j -lt ${#options[*]} ]]; do
		log_must $ZFS ${args[i]}=${options[j]} $TESTPOOL/$TESTFS

		$ZFS ls -L | $GREP "${args[i]}" | $GREP "${options[j]}"
		[[ $? -ne 0 ]] && \
			log_fail "Unable to verify ${args[i]}=${options[j]}"

		log_note "Verified ${args[i]}=${options[j]}"

		((j = j + 1))
	done

	((i = i + 1))
done

log_pass "zfs properties were set correctly."
