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
# ident	"@(#)cleanup.ksh	1.4	07/07/31 SMI"
#
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_upgrade/zpool_upgrade.kshlib

function destroy_upgraded_pool {
 for VERSION in $@
  do
    POOL_FILES=$($ENV | $GREP "ZPOOL_VERSION_${VERSION}_FILES"\
		 | $AWK -F= '{print $2}')
    POOL_NAME=$($ENV | $GREP "ZPOOL_VERSION_${VERSION}_NAME"\
  		 | $AWK -F= '{print $2}')
    poolexists $POOL_NAME
    if [ $? == 0 ]
    then
	log_must $ZPOOL destroy -f $POOL_NAME
    fi

  done
}

for config in $CONFIGS
do
   destroy_upgraded_pool $config
done
