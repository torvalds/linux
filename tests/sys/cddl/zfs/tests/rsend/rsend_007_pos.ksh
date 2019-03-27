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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)rsend_007_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_007_pos
#
# DESCRIPTION:
#	Rename parent filesystem name will not change the dependent order.
#
# STRATEGY:
#	1. Separately rename pool clone, filesystem and volume name.
#	2. Send -R all the POOL
#	3. Verify renamed dataset will not change the snapshot dependent order.
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

set -A 	dtst \
	"$POOL/pclone"		"$POOL/$FS/pclone"	\
	"$POOL/$FS/fs1/fs2"	"$POOL/fs2"
if is_global_zone ; then
	typeset -i n=${#dtst[@]}
	dtst[((n))]="$POOL/vol"; 	dtst[((n+1))]="$POOL/$FS/fs1/vol"
fi

function cleanup
{
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2

	log_must setup_test_model $POOL
}

log_assert "Rename parent filesystem name will not change the dependent order."
log_onexit cleanup

typeset -i i=0
while ((i < ${#dtst[@]})); do
	log_must $ZFS rename ${dtst[$i]} ${dtst[((i+1))]}

	((i += 2))
done

#
# Verify zfs send -R should succeed
#
log_must eval "$ZFS send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-final-R"
dstds=$(get_dst_ds $POOL $POOL2)
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

#
# Verify zfs send -R -I should succeed
#
log_must eval "$ZFS send -R -I @init $dstds@final > " \
        "$BACKDIR/pool-init-final-IR"
list=$(getds_with_suffix $dstds @snapA)
list="$list $(getds_with_suffix $dstds @snapB)"
list="$list $(getds_with_suffix $dstds @snapC)"
list="$list $(getds_with_suffix $dstds @final)"
log_must destroy_tree $list
if is_global_zone ; then
	log_must eval "$ZFS receive -d -F $dstds < $BACKDIR/pool-init-final-IR"
else
	$ZFS receive -d -F $dstds < $BACKDIR/pool-init-final-IR
fi
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

log_pass "Rename parent filesystem name will not change the dependent order."
