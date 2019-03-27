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
# ident	"@(#)reservation_017_pos.ksh	1.5	09/01/12 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/reservation/reservation.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: reservation_017_pos
#
# DESCRIPTION:
#
# For a sparse volume changes to the volsize are not reflected in the reservation
#
# STRATEGY:
# 1) Create a regular and sparse volume 
# 2) Get the space available in the pool
# 3) Set reservation with various size on the regular and sparse volume
# 4) Verify that the 'reservation' property for the regular volume has
#    the correct value.
# 5) Verify that the 'reservation' property for the sparse volume is set to 'none'
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

verify_runnable "global"

log_assert "Verify that the volsize changes of sparse volume are not reflected" \
	"in the reservation"

#Create a regular and sparse volume for testing.
regvol=$TESTPOOL/$TESTVOL
sparsevol=$TESTPOOL/$TESTVOL2
log_must $ZFS create -V $VOLSIZE -o volblocksize=16k $regvol
log_must $ZFS create -s -V $VOLSIZE -o volblocksize=16k $sparsevol

typeset -l vsize=$(get_prop available $TESTPOOL)
typeset -i iterate=10
typeset -l regreserv
typeset -l sparsereserv
typeset -l vblksize1=$(get_prop volblocksize $regvol)
typeset -l vblksize2=$(get_prop volblocksize $sparsevol)
typeset -l blknum=0
if [ "$vblksize1" != "$vblksize2" ]; then
	log_must $ZFS set volblocksize=$vblksize1 $sparsevol
fi
(( blknum = vsize / vblksize1 ))

typeset -i randomblknum
while (( iterate > 1 )); do
	(( randomblknum = 1 + $RANDOM % $blknum )) 
	#Make sure volsize is a multiple of volume block size
	(( vsize = $randomblknum * $vblksize1 ))
	log_must $ZFS set volsize=$vsize $regvol
	log_must $ZFS set volsize=$vsize $sparsevol
	regreserv=$(get_prop refreservation $regvol)
	sparsereserv=$(get_prop refreservation $sparsevol)
	reg_shouldreserv=$(zvol_volsize_to_reservation $vsize $vblksize1 1)

	(( $sparsereserv == $vsize )) && \
		log_fail "volsize changes of sparse volume is reflected in reservation."
	(( $regreserv != $reg_shouldreserv )) && \
		log_fail "volsize changes of regular volume isnot reflected in reservation."

	(( iterate = iterate - 1 ))
done

log_pass "The volsize change of sparse volume is not reflected in reservation as expected."
