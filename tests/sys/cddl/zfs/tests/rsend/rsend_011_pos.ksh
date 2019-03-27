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
# ident	"@(#)rsend_011_pos.ksh	1.2	09/08/06 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_011_pos
#
# DESCRIPTION:
#	Changes made by 'zfs inherit' can be properly received.
#
# STRATEGY:
#	1. Inherit property for filesystem and volume
#	2. Send and restore them in the target pool
#	3. Verify all the datasets can be properly backup and receive
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-10-10)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	unmount_all_safe > /dev/null 2>&1
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2

	log_must setup_test_model $POOL
}

log_assert "Verify changes made by 'zfs inherit' can be properly received."
log_onexit cleanup

#
# Setting all the $FS properties as local value,
#
for prop in $(fs_inherit_prop); do
	value=$(get_prop $prop $POOL/$FS)
	log_must $ZFS set $prop=$value $POOL/$FS
done

#
# Inherit propertes in sub-datasets
#
for ds in "$POOL/$FS/fs1" "$POOL/$FS/fs1/fs2" "$POOL/$FS/fs1/fclone" ; do
	for prop in $(fs_inherit_prop) ; do
		$ZFS inherit $prop $ds
		if (($? !=0 )); then
			log_fail "$ZFS inherit $prop $ds"
		fi
	done
done
if is_global_zone ; then
	for prop in $(vol_inherit_prop) ; do
		$ZFS inherit $prop $POOL/$FS/vol
		if (($? !=0 )); then
			log_fail "$ZFS inherit $prop $POOL/$FS/vol"
		fi
	done
fi

#
# Verify datasets can be backup and restore correctly
# Unmount $POOL/$FS to avoid two fs mount in the same mountpoint
#
log_must eval "$ZFS send -R $POOL@final > $BACKDIR/pool-R"
log_must $ZFS unmount -f $POOL/$FS
log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-R"

dstds=$(get_dst_ds $POOL $POOL2)
#
# Define all the POOL/POOL2 datasets pair
#
set -A pair 	"$POOL" 		"$dstds" 		\
		"$POOL/$FS" 		"$dstds/$FS" 		\
		"$POOL/$FS/fs1"		"$dstds/$FS/fs1"	\
		"$POOL/$FS/fs1/fs2"	"$dstds/$FS/fs1/fs2"	\
		"$POOL/pclone"		"$dstds/pclone"		\
		"$POOL/$FS/fs1/fclone"	"$dstds/$FS/fs1/fclone"

if is_global_zone ; then
	typeset -i n=${#pair[@]}
	pair[((n))]="$POOL/vol"; 	pair[((n+1))]="$dstds/vol"
	pair[((n+2))]="$POOL/$FS/vol"	pair[((n+3))]="$dstds/$FS/vol"
fi

#
# Verify all the sub-datasets can be properly received.
#
log_must cmp_ds_subs $POOL $dstds
typeset -i i=0
while ((i < ${#pair[@]})); do
	log_must cmp_ds_cont ${pair[$i]} ${pair[((i+1))]}
	log_must cmp_ds_prop ${pair[$i]} ${pair[((i+1))]}

	((i += 2))
done

log_pass "Changes made by 'zfs inherit' can be properly received."
