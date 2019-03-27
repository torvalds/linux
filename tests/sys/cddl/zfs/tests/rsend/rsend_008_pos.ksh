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
# ident	"@(#)rsend_008_pos.ksh	1.2	09/01/12 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_008_pos
#
# DESCRIPTION:
#	Changes made by 'zfs promote' can be properly received.
#
# STRATEGY:
#	1. Separately promote pool clone, filesystem clone and volume clone.
#	2. Recursively backup all the POOL and restore in POOL2
#	3. Verify all the datesets and property be properly received.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

#		Origin			Clone
#
set -A	dtst	"$POOL"			"$POOL/pclone"		\
		"$POOL/$FS/fs1/fs2"	"$POOL/$FS/fs1/fclone"
if is_global_zone ; then
	typeset -i n=${#dtst[@]}
	dtst[((n))]="$POOL/$FS/vol"; 	dtst[((n+1))]="$POOL/$FS/vclone"
fi

function cleanup
{
	typeset origin
	typeset -i i=0
	while ((i < ${#dtst[@]})); do
		origin=$(get_prop origin ${dtst[$i]})

		if [[ $origin != "-" ]]; then
			log_must $ZFS promote ${dtst[$i]}
		fi

		((i += 2))
	done

	origin=$(get_prop origin $POOL2)
	if [[ $origin != "-" ]]; then
		log_must $ZFS promote $POOL2
	fi
	log_must cleanup_pool $POOL2
}

log_assert "Changes made by 'zfs promote' can be properly received."
log_onexit cleanup

typeset -i i=0
while ((i < ${#dtst[@]})); do
	log_must $ZFS promote ${dtst[((i+1))]}

	((i += 2))
done

#
# Verify zfs send -R should succeed
#
log_must eval "$ZFS send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-final-R"

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

# Verify the original filesystem can be promoted
log_must $ZFS promote $dstds
if is_global_zone ; then
	log_must $ZFS promote $dstds/$FS/vol
fi
log_must $ZFS promote $dstds/$FS/fs1/fs2

log_pass "Changes made by 'zfs promote' can be properly received."
