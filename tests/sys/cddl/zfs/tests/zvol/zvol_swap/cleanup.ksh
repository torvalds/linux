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
# ident	"@(#)cleanup.ksh	1.3	08/05/14 SMI"
#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

verify_runnable "global"

log_must $SWAPADD 
for swapdev in $SAVESWAPDEVS
do
	if ! is_swap_inuse $swapdev ; then
		log_must $SWAP -a $swapdev >/dev/null 2>&1
	fi
done

voldev=/dev/zvol/$TESTPOOL/$TESTVOL
if is_swap_inuse $voldev ; then
	log_must $SWAP -d $voldev
fi

default_zvol_cleanup

log_pass
