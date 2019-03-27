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
# ident	"@(#)zpool_001_neg.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_001_neg
#
# DESCRIPTION:
# A badly formed sub-command passed to zpool(1) should
# return an error.
#
# STRATEGY:
# 1. Create an array containg each zpool sub-command name.
# 2. For each element, execute the sub-command.
# 3. Verify it returns an error.
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

verify_runnable "both"


set -A args "" "create" "add" "destroy" "import fakepool" \
    "export fakepool" "create fakepool" "add fakepool" \
    "create mirror" "create raidz" "create raidz1" \
    "create mirror fakepool" "create raidz fakepool" \
    "create raidz1 fakepool" "create raidz2 fakepool" \
    "create fakepool mirror" "create fakepool raidz" \
    "create fakepool raidz1" "create fakepool raidz2" \
    "add fakepool mirror" "add fakepool raidz" \
    "add fakepool raidz1" "add fakepool raidz2" \
    "add mirror fakepool" "add raidz fakepool" \
    "add raidz1 fakepool" "add raidz2 fakepool" \
    "setvprop" "blah blah" "-%" "--" "--?" "-*" "-="

log_assert "Execute zpool sub-command without proper parameters."

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot $ZPOOL ${args[i]}

	((i = i + 1))
done

log_pass "Badly formed zpool sub-commands fail as expected."
