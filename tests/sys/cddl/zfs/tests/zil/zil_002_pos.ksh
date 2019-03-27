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
# Copyright 2012 Spectra Logic Corporation.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zil/zil.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zil_002_pos
#
# DESCRIPTION:
#
# XXX XXX XXX
#
# STRATEGY:
# 1) XXX
# 2) XXX
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (YYYY-MM-DD) XXX
#
# __stc_assertion_end
#
###############################################################################  

verify_runnable "both"

td5=$TESTDIR/5
tf1=$td5/1
tf2=$td5/2
tf3=$td5/3
file_size=`expr $POOLSIZE / 4`
write_count=`expr $file_size / $BLOCK_SIZE`

function check_file
{
	typeset fname="$1"
	typeset -i expected_size="$2"

	log_must test -f $fname
	log_must test $expected_size == $(size_of_file $fname)
}

function cleanup
{
	ls -lr $TESTDIR
	log_must $RM -rf $TESTDIR/*
}

log_onexit cleanup

log_assert "Verify that creating and deleting content works"

# Run the pre-export tests.
zil_setup
log_must $MKDIR $td5
log_must $FILE_WRITE -o create -f $tf1 -b $BLOCK_SIZE -c $write_count -d 0
check_file $tf1 $file_size
log_must $CP $tf1 $tf2
log_must $CP $tf2 $tf3
check_file $tf2 $file_size
log_must $CMP $tf1 $tf2
log_must $RM -f $tf3

# Now run the post-export tests.
zil_reimport_pool $TESTPOOL
check_file $tf1 $file_size
check_file $tf2 $file_size
log_must $CMP $tf1 $tf2
log_mustnot test -f $tf3
log_must test -f $tf1
cur_file_size=$(size_of_file $tf1)
log_must test $file_size -eq $cur_file_size

log_pass "Success creating and deleting content"
