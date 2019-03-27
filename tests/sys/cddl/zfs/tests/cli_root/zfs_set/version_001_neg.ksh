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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)version_001_neg.ksh	1.2	08/08/15 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_upgrade/zfs_upgrade.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: version_001_neg
#
# DESCRIPTION:
# Valid version values should be positive integers only.
#
# STRATEGY:
# 1) Form an array of invalid reservation values (negative and
# incorrectly formed)
# 2) Attempt to set each invalid version value in turn on a
# filesystem and volume.
# 3) Verify that attempt fails and the version value remains
# unchanged
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

if ! fs_prop_exist "version" ; then
	log_unsupported "version is not supported by this release."
fi

log_assert "Verify invalid version values are rejected"

set -A values '' '-1' '-1.0' '-1.8' '-9999999999999999' \
	'0x1' '0b' '1b' '1.1b' '0' '0.000' '1.234'

# 
# Function to loop through a series of bad reservation
# values, checking they are when we attempt to set them
# on a dataset.
#
function set_n_check # data-set
{ 
	typeset obj=$1
	typeset -i i=0
	typeset -i j=0

	orig_val=$(get_prop version $obj)

	while (( $i < ${#values[*]} )); do
		$ZFS set version=${values[$i]} $obj  > /dev/null 2>&1
		if [[ $? -eq 0 ]]; then
			log_note "$ZFS set version=${values[$i]} $obj"
			log_fail "The above version set returned 0!"
		fi
		
		new_val=$(get_prop version $obj)

		if [[ $new_val != $orig_val ]]; then
			log_fail "$obj : version values changed " \
				"($orig_val : $new_val)"
		fi

		(( i = i + 1 ))
	done
}

for dataset in $TESTPOOL/$TESTFS $TESTPOOL/$TESTCTR $TESTPOOL/$TESTVOL
do
	set_n_check $dataset
done

log_pass "Invalid version values correctly rejected"
