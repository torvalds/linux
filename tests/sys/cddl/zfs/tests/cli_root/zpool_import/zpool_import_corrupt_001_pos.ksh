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
# Copyright 2013 Spectra Logic  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_014_pos
#
# DESCRIPTION:
#	Verify that a disk-backed exported pool with some of its vdev labels
#	corrupted can still be imported
# STRATEGY:
#	1. Create a disk-backed pool
#	2. Export it
#	3. Overwrite one or more of its vdev labels
#	4. Use zdb to verify that the labels are damaged
#	5. Verify 'zpool import' can import it
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2013-03-15)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

# ZFS has four vdev labels per vdev
typeset -i N_VDEV_LABELS=4
# Size of a single label, in bytes
typeset -i VDEV_LABEL_SIZE=$(( 256 * 1024))


#
# The authoritative version of this calculation can be found in the function of
# the same name in vdev_label.c.  The rounding of psize is based on the
# calculation in vdev_disk_read_rootlabel in vdev_disk.c
#
# arg1:	vdev size in bytes
# arg2: label index, 0 through 3
#
function vdev_label_offset
{
	typeset -il psize=$1
	typeset -i  l=$2
	typeset -il offset
	typeset -il roundsize

	roundsize=$(( $psize & -$VDEV_LABEL_SIZE ))
	if [[ $l -lt $(( N_VDEV_LABELS / 2 )) ]]; then
		offset=$(( l * $VDEV_LABEL_SIZE))
	else
		offset=$(( l * $VDEV_LABEL_SIZE + $roundsize - $N_VDEV_LABELS * $VDEV_LABEL_SIZE ))
	fi
	echo $offset
}

log_assert "Verify that a disk-backed exported pool with some of its vdev labels corrupted can still be imported"

typeset -i i
typeset -i j
set -A DISKS_ARRAY $DISKS
typeset DISK=${DISKS_ARRAY[0]}
typeset PROV=${DISK#/dev/}
typeset -il psize=$(geom disk list $PROV | awk '/Mediasize/ {print $2}')
if [[ -z $psize ]]; then
	log_fail "Could not determine the capacity of $DISK"
fi

for ((i=0; $i<$N_VDEV_LABELS; i=$i+1 )); do
	log_must $ZPOOL create -f $TESTPOOL $DISK
	log_must $ZPOOL export $TESTPOOL

	# Corrupt all labels except the ith
	for ((j=0; $j<$N_VDEV_LABELS; j=$j+1 )); do
		typeset -il offset

		[[ $i -eq $j ]] && continue

		log_note offset=vdev_label_offset $psize $j
		offset=$(vdev_label_offset $psize $j)
		log_must $DD if=/dev/zero of=$DISK bs=1024 \
			count=$(( $VDEV_LABEL_SIZE / 1024 )) \
			oseek=$(( $offset / 1024 )) \
			conv=notrunc
	done

	typeset -i num_labels=$( $ZDB -l $DISK | $GREP pool_guid | wc -l )
	if [[ $num_labels -ne 1 ]]; then
		$ZDB -l $DISK
		log_fail "Expected 1 vdev label but found $num_labels"
	fi

	log_must $ZPOOL import $TESTPOOL
	destroy_pool $TESTPOOL
done

log_pass
