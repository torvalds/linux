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
# ident	"@(#)zfs_create_010_neg.ksh	1.4	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/properties.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_create_010_neg
#
# DESCRIPTION:
# 'zfs create [-b <blocksize> ] -V <size> <volume>' fails with badly formed 
# <size> or <volume> arguments,including:
#	*Invalid volume size and volume name
#	*Invalid blocksize
#	*Incomplete component in the dataset tree
#	*The volume already exists
#	*The volume name beyond the maximal name length - 256.
#       *Same property set multiple times via '-o property=value' 
#       *Filesystems's property set on volume
#
# STRATEGY:
# 1. Create an array of badly formed arguments
# 2. For each argument, execute 'zfs create -V <size> <volume>'
# 3. Verify an error is returned.
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

verify_runnable "global"

function cleanup
{
	typeset -i i
	typeset found

	#
	# check to see if there is any new fs created during the test
	# if so destroy it.
	#
	for dset in $($ZFS list -H | \
		$AWK '{print $1}' | $GREP / ); do
		found=false
		i=0
		while (( $i < ${#existed_fs[*]} )); do
			if [[ $dset == ${existed_fs[i]} ]]; then
				found=true
				break
			fi
			(( i = i  + 1 ))
		done

		#
		# new fs created during the test, cleanup it
		#
		if [[ $found == "false" ]]; then
			log_must $ZFS destroy -f $dset
		fi
	done
}

log_onexit cleanup

log_assert "Verify 'zfs create [-s] [-b <blocksize> ] -V <size> <volume>' fails with" \
    "badly-formed <size> or <volume> arguments."

set -A args "$VOLSIZE" "$TESTVOL1" \
	"$VOLSIZE $TESTVOL1" "0 $TESTPOOL/$TESTVOL1" \
	"-1gb $TESTPOOL/$TESTVOL1" "1g? $TESTPOOL/$TESTVOL1" \
	"1.01BB $TESTPOOL/$TESTVOL1" "1%g $TESTPOOL/$TESTVOL1" \
	"1g% $TESTPOOL/$TESTVOL1" "1g$ $TESTPOOL/$TESTVOL1" \
	"$m $TESTPOOL/$TESTVOL1" "1m$ $TESTPOOL/$TESTVOL1" \
	"1m! $TESTPOOL/$TESTVOL1" \
	"1gbb $TESTPOOL/blah" "1blah $TESTPOOL/blah" "blah $TESTPOOL/blah" \
	"$VOLSIZE $TESTPOOL" "$VOLSIZE $TESTPOOL/" "$VOLSIZE $TESTPOOL//blah"\
	"$VOLSIZE $TESTPOOL/blah@blah" "$VOLSIZE $TESTPOOL/blah^blah" \
	"$VOLSIZE $TESTPOOL/blah*blah" "$VOLSIZE $TESTPOOL/blah%blah" \
	"$VOLSIZE blah" "$VOLSIZE $TESTPOOL/$BYND_MAX_NAME" \
	"1m -b $TESTPOOL/$TESTVOL1" "1m -b 11k $TESTPOOL/$TESTVOL1" \
	"1m -b 511 $TESTPOOL/$TESTVOL1" "1m -b 256k $TESTPOOL/$TESTVOL1"

set -A options "" "-s"

datasetexists $TESTPOOL/$TESTVOL || \
		log_must $ZFS create -V $VOLSIZE $TESTPOOL/$TESTVOL

set -A existed_fs $($ZFS list -H | $AWK '{print $1}' | $GREP / )

log_mustnot $ZFS create -V $VOLSIZE $TESTPOOL/$TESTVOL
log_mustnot $ZFS create -s -V $VOLSIZE $TESTPOOL/$TESTVOL

typeset -i i=0
typeset -i j=0
while (( i < ${#options[*]} )); do

	j=0
	while (( j < ${#args[*]} )); do
		log_mustnot $ZFS create ${options[$i]} -V ${args[$j]}
		log_mustnot $ZFS create -p ${options[$i]} -V ${args[$j]}

		((j = j + 1))
	done

	j=0
	while (( $j < ${#RW_VOL_PROP[*]} )); do
        	log_mustnot $ZFS create ${options[$i]} -o ${RW_VOL_PROP[j]} \
			-o ${RW_VOL_PROP[j]} -V $VOLSIZE $TESTPOOL/$TESTVOL1
        	log_mustnot $ZFS create -p ${options[$i]} -o ${RW_VOL_PROP[j]} \
			-o ${RW_VOL_PROP[j]} -V $VOLSIZE $TESTPOOL/$TESTVOL1
		((j = j + 1))
	done

	j=0
	while (( $j < ${#FS_ONLY_PROP[*]} )); do
		log_mustnot $ZFS create ${options[$i]} -o ${FS_ONLY_PROP[j]} \
			-V $VOLSIZE $TESTPOOL/$TESTVOL1
		log_mustnot $ZFS create -p ${options[$i]} -o ${FS_ONLY_PROP[j]} \
			-V $VOLSIZE $TESTPOOL/$TESTVOL1
        	((j = j + 1))
	done

	((i = i + 1))
done

log_pass "'zfs create [-s][-b <blocksize>] -V <size> <volume>' fails as expected."
