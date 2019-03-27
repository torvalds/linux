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
# ident	"@(#)zfs_get_005_neg.ksh	1.5	09/06/22 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_common.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_get_005_neg
#
# DESCRIPTION:
# Setting the invalid option and properties, 'zfs get' should failed.
#
# STRATEGY:
# 1. Create pool, filesystem, volume and snapshot.
# 2. Getting incorrect combination by invalid parameters
# 3. Using the combination as the parameters of 'zfs get' to check the
# command line return value.
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

set -A val_opts p r H
set -A v_props type used available creation volsize referenced compressratio mounted \
	origin recordsize quota reservation mountpoint sharenfs checksum \
	compression atime devices exec readonly setuid zoned snapdir aclmode \
	aclinherit canmount primarycache secondarycache \
	usedbychildren usedbydataset usedbyrefreservation usedbysnapshots

$ZFS upgrade -v > /dev/null 2>&1
if [[ $? -eq 0 ]]; then
	set -A v_props ${v_props[*]} version
fi

if is_userquota_supported; then
	set -A  userquota_props userquota@root groupquota@root \
		userused@root groupused@root	
fi

set -A val_pros -- "${v_props[@]}" "${userquota_props[@]}"

set -f	# Force shell does not parse '?' and '*' as the wildcard
set -A inval_opts P R h ? * 
set -A inval_props Type 0 ? * -on --on readonl time USED RATIO MOUNTED

set -A dataset $TESTPOOL/$TESTFS $TESTPOOL/$TESTCTR $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTVOL@$TESTSNAP 

typeset -i opt_numb=6
typeset -i prop_numb=12

val_opts_str=$(gen_option_str "${val_opts[*]}" "-" "" $opt_numb)
val_props_str=$(gen_option_str "${val_props[*]}" "" "," $prop_numb) 
val_props_str="$val_props_str -a -d"

inval_opts_str=$(gen_option_str "${inval_opts[*]}" "-" "" $opt_numb)
inval_props_str=$(gen_option_str "${inval_props[*]}" "" "," $prop_numb)

#
# Test different options and properties combination.
#
# $1 options
# $2 properties
#
function test_options
{
	typeset opts=$1
	typeset props=$2

	for dst in ${dataset[@]}; do
		for opt in $opts; do
			for prop in $props; do
				$ZFS get $opt $prop $dst > /dev/null 2>&1
				ret=$?
				if [[ $ret == 0 ]]; then
					log_fail "$ZFS get \
    $opt $prop $dst unexpectedly succeeded."
				fi
			done
		done
	done
}

log_assert "Setting the invalid option and properties, 'zfs get' should be \
	failed."
log_onexit cleanup

# Create filesystem and volume's snapshot
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTVOL $TESTSNAP

log_note "Valid options + invalid properties, 'zfs get' should fail."
test_options "$val_opts_str" "$inval_props_str"

log_note "Invalid options + valid properties, 'zfs get' should fail."
test_options "$inval_opts_str" "$val_props_str"

log_note "Invalid options + invalid properties, 'zfs get' should fail."
test_options "$inval_opts_str" "$inval_props_str"

log_pass "Setting the invalid options to dataset, 'zfs get' pass."
