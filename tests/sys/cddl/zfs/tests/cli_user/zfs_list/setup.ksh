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
# ident	"@(#)setup.ksh	1.6	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_list_d.kshlib

DISK=${DISKS%% *}

default_setup_noexit $DISK

# create datasets and set checksum options
set -A cksumarray $CKSUMOPTS
typeset -i index=0
for dataset in $DATASETS
do
	log_must $ZFS create $TESTPOOL/$TESTFS/$dataset
	enc=$(get_prop encryption $TESTPOOL/$TESTFS/$dataset)
	if [[ $? -eq 0 ]] && [[ -n "$enc" ]] && [[ "$enc" != "off" ]]; then
		log_unsupported "checksum property can't be changed \
when encryption is set to on."
	fi
	$SLEEP 1
        log_must $ZFS snapshot $TESTPOOL/$TESTFS/${dataset}@snap

	$SLEEP 1
	if is_global_zone ; then	
		log_must $ZFS create -V 64M $TESTPOOL/$TESTFS/${dataset}-vol
		$SLEEP 1
		log_must $ZFS snapshot $TESTPOOL/$TESTFS/${dataset}-vol@snap
	fi

	# sleep to ensure that the datasets have different creation dates
	$SLEEP 1
	log_must $ZFS set checksum=${cksumarray[$index]} \
		$TESTPOOL/$TESTFS/$dataset
	if datasetexists $TESTPOOL/$TESTFS/${dataset}-vol; then	
        	log_must $ZFS set checksum=${cksumarray[$index]} \
	    		$TESTPOOL/$TESTFS/${dataset}-vol
	fi
	
        index=$((index + 1))
done

if zfs_get_list_d_supported ; then
	depth_fs_setup
fi

log_pass
