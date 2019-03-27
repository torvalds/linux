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
# ident	"@(#)property_alias_001_pos.ksh	1.4	09/01/13 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: property_alias_001_pos
#
# DESCRIPTION:
# Verify the properties with aliases also work with those aliases
#
# STRATEGY:
# 1. Create pool, then create filesystem & volume within it.
# 2. Set or retrieve property via alias with datasets.
# 3. Verify the result should be successful.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-16)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function set_and_check #<dataset><set_prop_name><set_value><check_prop_name>
{
	typeset ds=$1
	typeset setprop=$2
	typeset setval=$3
	typeset chkprop=$4
	typeset getval

	log_must $ZFS set $setprop=$setval $ds
	if [[ $setval == "gzip-6" ]]; then
		setval="gzip"
	fi
	getval=$(get_prop $chkprop $ds)	
	
	case $setprop in
		reservation|reserv )
               		if [[ $setval == "none" ]]; then
				 [[ $getval != "0" ]] && \
					log_fail "Setting the property $setprop" \
						"with value $setval fails."
                        elif [[ $getval != $setval ]]; then
				log_fail "Setting the property $setprop with" \
					"with $setval fails."
			fi
                        ;;
                 * )
                        [[ $getval != $setval ]] && \
				log_fail "Setting the property $setprop with value \
					$setval fails."
                        ;;
         esac
}

log_assert "Properties with aliases also work with those aliases."

set -A ro_prop "available" "avail" "referenced" "refer"
set -A rw_prop "readonly" "rdonly" "compression" "compress" "reservation" "reserv"
set -A chk_prop "rdonly" "readonly" "compress" "compression" "reserv" "reservation"
set -A size "512" "1024" "2048" "4096" "8192" "16384" "32768" "65536" "131072"

pool=$TESTPOOL
fs=$TESTPOOL/$TESTFS
vol=$TESTPOOL/$TESTVOL
typeset -l avail_space=$(get_prop avail $pool)
typeset -l reservsize
typeset -i i=0

for ds in $pool $fs $vol; do
	for propname in ${ro_prop[*]}; do
		$ZFS get -pH -o value $propname $ds >/dev/null 2>&1
		(( $? != 0 )) && \
			log_fail "Get the property $proname of $ds failed."
	done
	i=0
	while (( i < ${#rw_prop[*]} )); do
		case ${rw_prop[i]} in
		readonly|rdonly )
			for val in "on" "off"; do
				set_and_check $ds ${rw_prop[i]} $val ${chk_prop[i]} 
			done
			;;
		compression|compress )
			for val in $(get_compress_opts zfs_set); do
				set_and_check $ds ${rw_prop[i]} $val ${chk_prop[i]} 
			done
			;;
		reservation|reserv )
			(( reservsize = $avail_space % $RANDOM ))
			for val in "0" "$reservsize" "none"; do
				set_and_check $ds ${rw_prop[i]} $val ${chk_prop[i]} 
			done
			;;
		esac

		(( i = i + 1 ))
	done	
	if [[ $ds == $vol ]]; then
		for propname in "volblocksize" "volblock" ; do
			$ZFS get -pH -o value $propname $ds >/dev/null 2>&1
			(( $? != 0 )) && \
				log_fail "Get the property $propname of $ds failed."
		done
	fi
done

for ds in $pool $fs; do
	for propname in "recordsize" "recsize"; do
		for val in ${size[*]}; do
			if [[ $propname == "recordsize" ]]; then
				set_and_check $ds $propname $val "recsize"
			else
				set_and_check $ds $propname $val "recordsize"
			fi
		done
	done
done

log_pass "The alias of a property works as expected."
