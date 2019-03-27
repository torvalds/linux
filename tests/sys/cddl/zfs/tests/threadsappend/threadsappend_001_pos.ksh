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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)threadsappend_001_pos.ksh	1.2	07/01/09 SMI"
#

. ${STF_SUITE}/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: threadsappend_001_pos
#
# DESCRIPTION:
#
# Ensure multiple threads performing write appends to the same ZFS
# file succeed.
#
# STRATEGY:
#	1) Verify this is a multi-processor system
#	2) Create multiple threads with each appending to a file
#       3) Verify that the resulting file is the expected size
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "both"

log_assert "Ensure multiple threads performing write appends to the same" \
	"ZFS file succeed"

#
# $FILE_SIZE is hardcoded into threadsappend.c and is the expected
# size of the file after all the threads have appended to it
#
typeset -i FILE_SIZE=1310720

#
# This test should be run on a multi-processor system because otherwise the FS
# will not be concurrently used by the threads
#
NCPUS=`sysctl -a | awk -F '"' '/cpu count="[0-9+]"/ {print $2; exit}'`
if [[ $? -ne 0 || -z $NCPUS || $NCPUS -le 1 ]]; then
	 log_unsupported \
	 	"This test should be executed on a multi-processor system."
fi

#
# zfs_threadsappend tries to append to $TESTFILE using threads
# so that the resulting file is $FILE_SIZE bytes in size
#
log_must threadsappend ${TESTDIR}/${TESTFILE}

#
# Check the size of the resulting file
#
SIZE=`$LS -l ${TESTDIR}/${TESTFILE} | $AWK '{print $5}'`
if [[ $SIZE -ne $FILE_SIZE ]]; then
	log_fail "'The length of ${TESTDIR}/${TESTFILE}' doesnt equal 1310720." 
fi

log_pass "Multiple thread appends succeeded. File size as expected"
