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
# Copyright 2014 Spectra Logic Corp.  All rights reserved.
# Use is subject to license terms.
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib


set_disks

set -A args "add -n $TESTPOOL $DISK1" "add -fn $TESTPOOL $DISK1"

log_assert "zpool add [-f] -n succeeds for unpriveleged users"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_must run_unprivileged "$ZPOOL ${args[i]}"
	((i = i + 1))
done

log_pass
