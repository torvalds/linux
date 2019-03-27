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
# ident	"@(#)zfs_get_001_pos.ksh	1.6	09/06/22 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_common.kshlib
. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_list_d.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_get_001_pos
#
# DESCRIPTION:
# Setting the valid option and properties, 'zfs get' should return the
# correct property value.
#
# STRATEGY:
# 1. Create pool, filesystem, volume and snapshot.
# 2. Setting valid parameter, 'zfs get' should succeed.
# 3. Compare the output property name with the original input property.
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

set -A options "" "-p" "-r" "-H"
if zfs_get_list_d_supported ; then
	typeset -i i=${#options[*]}
	typeset -i j=0
	while (( j<${#depth_options[*]} ));
	do
		options[$i]=-"${depth_options[$j]}"
		(( j+=1 ))
		(( i+=1 ))
	done
fi

set -A zfs_props type used available creation volsize referenced \
	compressratio mounted origin recordsize quota reservation mountpoint \
	sharenfs checksum compression atime devices exec readonly setuid \
	snapdir aclmode aclinherit canmount primarycache secondarycache \
	usedbychildren usedbydataset usedbyrefreservation usedbysnapshots


$ZFS upgrade -v > /dev/null 2>&1
if [[ $? -eq 0 ]]; then
	set -A zfs_props ${zfs_props[*]} version
fi

if is_userquota_supported; then
	set -A  userquota_props userquota@root groupquota@root \
		userused@root groupused@root	
fi

set -A all_props --  "${zfs_props[@]}" "${userquota_props[@]}"

set -A dataset $TESTPOOL/$TESTCTR $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTVOL@$TESTSNAP

#
# According to dataset and option, checking if 'zfs get' return correct
# property information.
#
# $1 dataset
# $2 properties which are expected to output into $TESTDIR/$TESTFILE0
# $3 option
#
function check_return_value
{
	typeset dst=$1
	typeset props=$2
	typeset opt=$3
	typeset -i found=0
	typeset p
	
	for p in $props; do
		found=0

		while read line; do
			typeset item
			item=$($ECHO $line | $AWK '{print $2}' 2>&1)

			if [[ $item == $p ]]; then
				(( found += 1 ))
				break
			fi
		done < $TESTDIR/$TESTFILE0

		if (( found == 0 )); then
			log_fail "'zfs get $opt $props $dst' return " \
				"error message.'$p' haven't been found."
		fi
	done

	log_note "SUCCESS: '$ZFS get $opt $prop $dst'."
}

log_assert "Setting the valid options and properties 'zfs get' should return " \
	"the correct property value."
log_onexit cleanup

# Create filesystem and volume's snapshot
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTVOL $TESTSNAP

typeset -i i=0
while (( i < ${#dataset[@]} )); do
	for opt in "${options[@]}"; do
		for prop in ${all_props[@]}; do
			eval "$ZFS get $opt $prop ${dataset[i]} > \
				$TESTDIR/$TESTFILE0"
			ret=$?
			if [[ $ret != 0 ]]; then
				log_fail "$ZFS get returned: $ret"
			fi
			check_return_value ${dataset[i]} "$prop" "$opt"
		done
	done
	(( i += 1 ))
done

log_pass "Setting the valid options to dataset, it should succeed and return " \
	"valid value. 'zfs get' pass."
