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
# ident	"@(#)checksum_001_pos.ksh	1.3	08/11/03 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: checksum_001_pos
#
# DESCRIPTION:
# Setting a valid checksum on a pool, file system, volume, it should be 
# successful.
#
# STRATEGY:
# 1. Create pool, then create filesystem and volume within it.
# 2. Setting different valid checksum to each dataset.
# 3. Check the return value and make sure it is 0.
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

enc=$(get_prop encryption $TESTPOOL/$TESTFS)
if [[ $? -eq 0 ]] && [[ -n "$enc" ]] && [[ "$enc" != "off" ]]; then
	log_unsupported "checksum property can not be changed when \
encryption is set to on."
fi

set -A dataset "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTVOL"
set -A values "on" "off" "fletcher2" "fletcher4" "sha256"

log_assert "Setting a valid checksum on a file system, volume," \
	"it should be successful."

typeset -i i=0
typeset -i j=0
while (( i < ${#dataset[@]} )); do
	j=0
	while (( j < ${#values[@]} )); do
		set_n_check_prop "${values[j]}" "checksum" "${dataset[i]}"
		(( j += 1 ))
	done
	(( i += 1 ))
done

log_pass "Setting a valid checksum on a file system, volume pass."
