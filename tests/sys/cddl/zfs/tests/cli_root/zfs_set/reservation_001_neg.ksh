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
# ident	"@(#)reservation_001_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/reservation/reservation.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: reservation_001_neg
#
# DESCRIPTION:
# Valid reservation values should be positive integers only.
#
# STRATEGY:
# 1) Form an array of invalid reservation values (negative and
# incorrectly formed)
# 2) Attempt to set each invalid reservation value in turn on a
# filesystem and volume.
# 3) Verify that attempt fails and the reservation value remains
# unchanged
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

log_assert "Verify invalid reservation values are rejected"

set -A suffix "b" "k" "m" "t" "p" "e" "K" "M" "G" "T" "P" "E" "kb" "Mb" "Gb" \
	"Tb" "Pb" "Eb" "KB" "MB" "GB" "TB" "PB" "EB" 

set -A values '' '-1' '-1.0' '-1.8' '-9999999999999999' '0x1' '0b' '1b' '1.1b'

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

	orig_resv_val=$(get_prop reservation $obj)

	while (( $i < ${#values[*]} )); do
		j=0
		while (( $j < ${#suffix[*]} )); do

			$ZFS set \
				reservation=${values[$i]}${suffix[$j]} $obj \
				> /dev/null 2>&1
			if [ $? -eq 0 ]
			then
				log_note "$ZFS set \
				reservation=${values[$i]}${suffix[$j]} $obj"
				log_fail "The above reservation set returned 0!"
			fi
		
			new_resv_val=$(get_prop reservation $obj)

			if [[ $new_resv_val != $orig_resv_val ]]; then
				log_fail "$obj : reservation values changed " \
					"($orig_resv_val : $new_resv_val)"
			fi
			(( j = j + 1 ))
		done

	(( i = i + 1 ))
	done
}

for dataset in $TESTPOOL/$TESTFS $TESTPOOL/$TESTCTR $TESTPOOL/$TESTVOL
do
	set_n_check $dataset
done

log_pass "Invalid reservation values correctly rejected"
