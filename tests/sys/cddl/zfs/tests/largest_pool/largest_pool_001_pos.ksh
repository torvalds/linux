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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)largest_pool_001_pos.ksh	1.6	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib

# ##########################################################################
#
# start __stf_assertion__
#
# ASSERTION: largest_pool_001
#
# DESCRIPTION:
#	The largest pool can be created and a dataset in that
#	pool can be created and mounted.
#
# STRATEGY:
#	create a pool which will contain a volume device.
#	create a volume device of desired sizes.
#	create the largest pool allowed using the volume vdev.
#	create and mount a dataset in the largest pool.
#	create some files in the zfs file system.
#	do some zpool list commands and parse the output.
#
# end __stf_assertion__
#
# ##########################################################################

verify_runnable "global"

#
# Parse the results of zpool & zfs creation with specified size
#
# $1: volume size
#
# return value:
# 0 -> success
# 1 -> failure
#
function parse_expected_output
{
	UNITS=`$ECHO $1 | $SED -e 's/^\([0-9].*\)\([a-z].\)/\2/'`
	case "$UNITS" in
		'mb') CHKUNIT="M" ;;
		'gb') CHKUNIT="G" ;;
		'tb') CHKUNIT="T" ;;
		'pb') CHKUNIT="P" ;;
		'eb') CHKUNIT="E" ;;
		*) CHKUNIT="M" ;;
	esac

	log_note "Detect zpool $TESTPOOL in this test machine."
	log_must eval "$ZPOOL list $TESTPOOL > $TMPDIR/j.${TESTCASE_ID}"
	log_must eval "$GREP $TESTPOOL $TMPDIR/j.${TESTCASE_ID} | \
		$AWK '{print $2}' | $GREP $CHKUNIT"
	
	log_note "Detect the file system in this test machine."
	log_must eval "$DF -t zfs -h > $TMPDIR/j.${TESTCASE_ID}"
	log_must eval "$GREP $TESTPOOL $TMPDIR/j.${TESTCASE_ID} | \
		$AWK '{print $2}' | $GREP $CHKUNIT"

	return 0
}

#
# Check and destroy zfs, volume & zpool remove the temporary files
#
function cleanup
{
	log_note "Start cleanup the zfs and pool"

	if datasetexists $TESTPOOL/$TESTFS ; then
		if ismounted $TESTPOOL/$TESTFS ; then
			log_must $ZFS unmount $TESTPOOL/$TESTFS
		fi
		log_must $ZFS destroy $TESTPOOL/$TESTFS
	fi

	destroy_pool $TESTPOOL

	datasetexists $TESTPOOL2/$TESTVOL && \
		log_must $ZFS destroy $TESTPOOL2/$TESTVOL
	
	destroy_pool $TESTPOOL2

	$RM -f $TMPDIR/j.* > /dev/null
}

log_assert "The largest pool can be created and a dataset in that" \
	"pool can be created and mounted."

# Set trigger. When the test case exit, cleanup is executed.
log_onexit cleanup

# -----------------------------------------------------------------------
# volume sizes with unit designations.
#
# Note: specifying the number '1' as size will not give the correct
# units for 'df'.  It must be greater than one.
# -----------------------------------------------------------------------
typeset str
typeset -i ret
for volsize in $VOLSIZES; do
	log_note "Create a pool which will contain a volume device"
	create_pool $TESTPOOL2 "$DISKS"

	log_note "Create a volume device of desired sizes: $volsize"
	str=$($ZFS create -sV $volsize $TESTPOOL2/$TESTVOL 2>&1)
	ret=$?
	if (( ret != 0 )); then
		if [[ $($ISAINFO -b) == 32 && \
			$str == *${VOL_LIMIT_KEYWORD1}* || \
			$str == *${VOL_LIMIT_KEYWORD2}* || \
			$str == *${VOL_LIMIT_KEYWORD3}* ]]
		then
			log_unsupported \
				"Max volume size is 1TB on 32-bit systems."
		else
			log_fail "$ZFS create -sV $volsize $TESTPOOL2/$TESTVOL"
		fi
	fi

	log_note "Create the largest pool allowed using the volume vdev"
	create_pool $TESTPOOL "$VOL_PATH"

	log_note "Create a zfs file system in the largest pool"
	log_must $ZFS create $TESTPOOL/$TESTFS

	log_note "Parse the execution result"
	parse_expected_output $volsize

	log_note "unmount this zfs file system $TESTPOOL/$TESTFS"
	log_must $ZFS unmount $TESTPOOL/$TESTFS

	log_note "Destroy zfs, volume & zpool"
	log_must $ZFS destroy $TESTPOOL/$TESTFS
	destroy_pool $TESTPOOL
	log_must $ZFS destroy $TESTPOOL2/$TESTVOL
	destroy_pool $TESTPOOL2
done

log_pass "Dateset can be created, mounted & destroy in largest pool succeeded."
