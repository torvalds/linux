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
# ident	"@(#)refquota_006_neg.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: refquota_006_neg
#
# DESCRIPTION:
#	'zfs set refquota/refreserv' can handle incorrect arguments correctly.
#
# STRATEGY:
#	1. Setup incorrect arguments arrays.
#	2. Set the bad argument to refquota.
#	3. Verify zfs can handle it correctly.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-11-09)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	log_must $ZFS set refquota=none $TESTPOOL/$TESTFS
	log_must $ZFS set refreserv=none $TESTPOOL/$TESTFS
}

log_assert "'zfs set refquota' can handle incorrect arguments correctly."
log_onexit cleanup

set -A badopt	\
	"None"		"-1"		"1TT"		"%5"		\
	"123!"		"@456"		"7#89" 		"0\$"		\
	"abc123%"	"123%s"		"12%s3"		"%c123"		\
	"123%d"		"%x123"		"12%p3" 	"^def456" 	\
	"x0"

typeset -i i=0
while ((i < ${#badopt[@]})); do
	log_mustnot $ZFS set refquota=${badopt[$i]} $TESTPOOL/$TESTFS
	log_mustnot $ZFS set refreserv=${badopt[$i]} $TESTPOOL/$TESTFS

	((i += 1))
done

# Try using a null as the opt value.  We can't use log_mustnot, because
# that echoes the character, which screws up ATF by creating a non-well formed
# XML file
$ZFS set refquota="\0" $TESTPOOL/$TESTFS > /dev/null 2>&1
[[ $? != 0 ]] || log_fail "FAILURE: zfs set refquota=\\\\0 passed"
$ZFS set refreserv="\0" $TESTPOOL/$TESTFS > /dev/null 2>&1
[[ $? != 0 ]] || log_fail "FAILURE: zfs set refreserv=\\\\0 passed"

log_pass "'zfs set refquota' can handle incorrect arguments correctly."
