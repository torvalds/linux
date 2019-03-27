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
# ident	"@(#)zpool_create_001_pos.ksh	1.4	09/05/19 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_create/zpool_create.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_001_pos
#
# DESCRIPTION:
# 'zpool create <pool> <vspec> ...' can successfully create a
# new pool with a name in ZFS namespace.
#
# STRATEGY:
# 1. Create storage pools with a name in ZFS namespace with different
# vdev specs.
# 2. Verify the pool created successfully
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
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	clean_blockfile "$TESTDIR0 $TESTDIR1"
}

log_assert "'zpool create <pool> <vspec> ...' can successfully create" \
	"a new pool with a name in ZFS namespace."

log_onexit cleanup

set -A keywords "" "mirror" "raidz" "raidz1"

typeset diskname0=${DISK0#/dev/}
typeset diskname1=${DISK1#/dev/}

case $DISK_ARRAY_NUM in 
0|1)
	typeset disk=""
	if (( $DISK_ARRAY_NUM == 0 )); then
		disk=$DISK
	else
		disk=$DISK0
	fi
	typeset diskname=${disk#/dev/}
	create_blockfile ${disk}p5 $TESTDIR0/$FILEDISK0
	create_blockfile ${disk}p6 $TESTDIR1/$FILEDISK1

	pooldevs="${diskname}p1 \
                  /dev/${diskname}p1 \
                  \"${diskname}p1 ${diskname}p2\" \
                  $TESTDIR0/$FILEDISK0"
	raidzdevs="\"/dev/${diskname}p1 ${diskname}p2\" \
                   \"${diskname}p1 ${diskname}p2 ${diskname}p3\" \
                   \"${diskname}p1 ${diskname}p2 ${diskname}p3 \
                     ${diskname}p4\"\
                   \"$TESTDIR0/$FILEDISK0 $TESTDIR1/$FILEDISK1\""
	mirrordevs=$raidzdevs
	;;
2|*)
	create_blockfile ${DISK0}p5 $TESTDIR0/$FILEDISK0
	create_blockfile ${DISK1}p5 $TESTDIR1/$FILEDISK1

	pooldevs="${diskname0}p1\
                 \"/dev/${diskname0}p1 ${diskname1}p1\" \
                 \"${diskname0}p1 ${diskname0}p2 ${diskname1}p2\"\
                 \"${diskname0}p1 ${diskname1}p1 ${diskname0}p2\
                   ${diskname1}p2\" \
                 \"$TESTDIR0/$FILEDISK0 $TESTDIR1/$FILEDISK1\""
 	raidzdevs="\"/dev/${diskname0}p1 ${diskname1}p1\" \
                 \"${diskname0}p1 ${diskname0}p2 ${diskname1}p2\"\
                 \"${diskname0}p1 ${diskname1}p1 ${diskname0}p2\
                   ${diskname1}p2\" \
                 \"$TESTDIR0/$FILEDISK0 $TESTDIR1/$FILEDISK1\""
	mirrordevs=$raidzdevs
	;;
esac

typeset -i i=0
while (( $i < ${#keywords[*]} )); do
	case ${keywords[i]} in
	"")
		create_pool_test "$TESTPOOL" "${keywords[i]}" "$pooldevs";;
	mirror)
		create_pool_test "$TESTPOOL" "${keywords[i]}" "$mirrordevs";;
	raidz|raidz1)
		create_pool_test "$TESTPOOL" "${keywords[i]}" "$raidzdevs" ;;
	esac
	(( i = i+1 ))
done

log_pass "'zpool create <pool> <vspec> ...' success."
