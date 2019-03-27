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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)reservation_003_pos.ksh	1.3	09/08/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/reservation/reservation.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: reservation_003_pos
#
# DESCRIPTION:
#
# Verify that it's possible to set a reservation on a filesystem,
# or volume multiple times, without resetting the reservation
# to none.
#
# STRATEGY:
# 1) Create a regular volume and a sparse volume
# 2) Get the space available in the pool
# 3) Set a reservation on the filesystem less than the space available.
# 4) Verify that the 'reservation' property for the filesystem has
# the correct value.
# 5) Repeat 2-4 for different reservation values
# 6) Repeat 3-5 for regular and sparse volume
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-19)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify it is possible to set reservations multiple times " \
	"on a filesystem regular and sparse volume"


#
# Set a reservation $RESV_ITER times on a dataset and verify that
# the reservation is correctly set each time.
#
function multiple_resv { #dataset
	typeset -i i=0

	dataset=$1

	log_must zero_reservation $dataset
	space_avail=`get_prop available $TESTPOOL`

	(( resv_size = ( space_avail - RESV_DELTA ) / RESV_ITER ))

	#
        # For regular (non-sparse) volumes the upper limit is determined
        # not by the space available in the pool but rather by the size
        # of the volume itself.
        #
        [[ $obj == $TESTPOOL/$TESTVOL ]] && \
                (( resv_size = ( vol_set_size - RESV_DELTA ) / RESV_ITER ))

	resv_size_set=$resv_size

	while (( $i < $RESV_ITER )); do

		(( i = i + 1 ))

		(( resv_size_set = resv_size * i ))

		log_must $ZFS set reservation=$resv_size_set $dataset

		resv_size_get=`get_prop reservation $dataset`
		if [[ $resv_size_set != $resv_size_get ]]; then
			log_fail "Reservation not the expected value " \
				"($resv_size_set != $resv_size_get)"
		fi
	done

	log_must zero_reservation $dataset
}

space_avail=`get_prop available $TESTPOOL`

if ! is_global_zone ; then
	OBJ_LIST=""
else
	OBJ_LIST="$TESTPOOL/$TESTVOL $TESTPOOL/$TESTVOL2"

	(( vol_set_size = space_avail / 4 ))
	vol_set_size=$(floor_volsize $vol_set_size)
	(( sparse_vol_set_size = space_avail * 4 ))
	sparse_vol_set_size=$(floor_volsize $sparse_vol_set_size)


	log_must $ZFS create -V $vol_set_size $TESTPOOL/$TESTVOL
	log_must $ZFS set reservation=none $TESTPOOL/$TESTVOL
	log_must $ZFS create -s -V $sparse_vol_set_size $TESTPOOL/$TESTVOL2
fi

for obj in $TESTPOOL/$TESTFS $OBJ_LIST ; do
	multiple_resv $obj
done

log_pass "Multiple reservations successfully set on filesystem" \
	" and both volume types"
