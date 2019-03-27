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
# ident	"@(#)reservation_001_pos.ksh	1.3	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/reservation/reservation.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: reservation_001_pos
#
# DESCRIPTION:
#
# ZFS allows reservations to be set on filesystems and volumes, provided
# the reservation is less than the space available in the pool. 
#
# STRATEGY:
# 1) Create a regular and sparse volume 
#   (filesystem already created by default_setup)
# 2) Get the space available in the pool
# 3) Set a reservation on the filesystem less than the space available.
# 4) Verify that the 'reservation' property for the filesystem has 
#    the correct value.
# 5) Reset the reservation to 'none'
# 6) Repeat steps 2-5 for both volume types
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-19)
#
# __stc_assertion_end
#
###############################################################################  

verify_runnable "both"

log_assert "Verify that to set a reservation on a filesystem" \
        " or volume must use value smaller than space" \
	" available property of pool"

space_avail=`get_prop available $TESTPOOL`

if ! is_global_zone ; then
	OBJ_LIST=""
else
	OBJ_LIST="$TESTPOOL/$TESTVOL $TESTPOOL/$TESTVOL2"

	(( vol_set_size = space_avail / 4 ))
	vol_set_size=$(floor_volsize $vol_set_size)
	(( sparse_vol_set_size = space_avail * 4 ))
	sparse_vol_set_size=$(floor_volsize $sparse_vol_set_size)

	#
	# Note that when creating a regular volume we are implicitly
	# setting a reservation upon it (i.e. the size of the volume)
	# which we reset back to zero initially.
	#
	log_must $ZFS create -V $vol_set_size $TESTPOOL/$TESTVOL
	log_must $ZFS set reservation=none $TESTPOOL/$TESTVOL
	if fs_prop_exist refreserv; then
		log_must $ZFS set refreservation=none $TESTPOOL/$TESTVOL
	fi
	log_must $ZFS create -s -V $sparse_vol_set_size $TESTPOOL/$TESTVOL2
fi


for obj in $TESTPOOL/$TESTFS $OBJ_LIST; do

        space_avail=`get_prop available $TESTPOOL`
        resv_size_set=`expr $space_avail - $RESV_DELTA`

	#
	# For a regular (non-sparse) volume the upper limit 
	# for reservations is not determined by the space 
	# available in the pool but rather by the size of 
	# the volume itself.
	#
        [[ $obj == $TESTPOOL/$TESTVOL ]] && \
                (( resv_size_set = vol_set_size - RESV_DELTA ))

        log_must $ZFS set reservation=$resv_size_set $obj

        resv_size_get=`get_prop reservation $obj`
        if [[ $resv_size_set != $resv_size_get ]]; then
                log_fail "Reservation not the expected value "\
                        "($resv_size_set != $resv_size_get)"
        fi

        log_must zero_reservation $obj

        new_space_avail=`get_prop available $obj`

        #
        # Due to the way space is consumed and released by metadata we
        # can't do an exact check here, but we do do a basic sanity
        # check.
        #
        log_must within_limits $space_avail $new_space_avail $RESV_TOLERANCE
done

log_pass "Successfully set reservation on filesystem and volume"
