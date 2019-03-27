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
# ident	"@(#)zfs_create_009_neg.ksh	1.4	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/properties.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_create_009_neg
#
# DESCRIPTION:
# 'zfs create <filesystem>' fails with bad <filesystem> arguments, including:
# 	*Invalid character against the ZFS namespace
#	*Incomplete component
#	*Too many arguments
#	*Filesystem already exists 
#	*Beyond maximal name length.
#	*Same property set multiple times via '-o property=value' 
#	*Volume's property set on filesystem
#
# STRATEGY:
# 1. Create an array of <filesystem> arguments
# 2. Execute 'zfs create <filesystem>' with each argument
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

verify_runnable "both"

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

set -A args  "$TESTPOOL/" "$TESTPOOL//blah" "$TESTPOOL/@blah" \
	"$TESTPOOL/blah@blah" "$TESTPOOL/blah^blah" "$TESTPOOL/blah%blah" \
	"$TESTPOOL/blah*blah" "$TESTPOOL/blah blah" \
	"-s $TESTPOOL/$TESTFS1" "-b 1092 $TESTPOOL/$TESTFS1" \
	"-b 64k $TESTPOOL/$TESTFS1" "-s -b 32k $TESTPOOL/$TESTFS1" \
	"$TESTPOOL/$BYND_MAX_NAME" 

log_assert "Verify 'zfs create <filesystem>' fails with bad <filesystem> argument."

datasetexists $TESTPOOL/$TESTFS || \
	log_must $ZFS create $TESTPOOL/$TESTFS

set -A existed_fs $($ZFS list -H | $AWK '{print $1}' | $GREP / )

log_mustnot $ZFS create $TESTPOOL
log_mustnot $ZFS create $TESTPOOL/$TESTFS

typeset -i i=0
while (( $i < ${#args[*]} )); do
	log_mustnot $ZFS create ${args[$i]}
	log_mustnot $ZFS create -p ${args[$i]}
	((i = i + 1))
done

i=0
while (( $i < ${#RW_FS_PROP[*]} )); do
	log_mustnot $ZFS create -o ${RW_FS_PROP[i]} -o ${RW_FS_PROP[i]} \
		$TESTPOOL/$TESTFS1
	log_mustnot $ZFS create -p -o ${RW_FS_PROP[i]} -o ${RW_FS_PROP[i]} \
		$TESTPOOL/$TESTFS1
	((i = i + 1))
done

i=0
while (( $i < ${#VOL_ONLY_PROP[*]} )); do
	log_mustnot $ZFS create -o ${VOL_ONLY_PROP[i]} $TESTPOOL/$TESTFS1
	log_mustnot $ZFS create -p -o ${VOL_ONLY_PROP[i]} $TESTPOOL/$TESTFS1
	((i = i + 1))
done

log_pass "'zfs create <filesystem>' fails as expected with bad <filesystem> argument."
