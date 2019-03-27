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
# ident	"@(#)pool_names_002_neg.ksh	1.4	08/11/03 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: pool_names_002_neg
#
# DESCRIPTION:
#
# Ensure that a set of invalid names cannot be used to create pools.
#
# STRATEGY:
# 1) For each invalid character in the character set, try to create
# and destroy the pool. Verify it fails.
# 2) Given a list of invalid pool names, ensure the pools are not
# created.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-11-21)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Ensure that a set of invalid names cannot be used to create pools."

# Global variable use to cleanup failures.
POOLNAME=""

function cleanup
{
	destroy_pool $POOLNAME

	if [[ -d $TESTDIR ]]; then
		log_must $RM -rf $TESTDIR
	fi
}

log_onexit cleanup

if [[ ! -e $TESTDIR ]]; then
        log_must $MKDIR $TESTDIR
fi

log_note "Ensure invalid characters fail"
for POOLNAME in "!" "\"" "#" "$" "%" "&" "'" "(" ")" \
    "\*" "+" "," "-" "\." "/" "\\" \
    ":" ";" "<" "=" ">" "\?" "@" \
    "[" "]" "^" "_" "\`" "{" "|" "}" "~"
do
	log_mustnot $ZPOOL create -m $TESTDIR $POOLNAME $DISK
        if poolexists $POOLNAME; then
                log_fail "Unexpectedly created pool: '$POOLNAME'"
        fi

	log_mustnot $ZPOOL destroy $POOLNAME
done

# poolexists cannot be used to test pools with numeric names, because
# "zpool list" will interpret the name as a repeat interval and never return.
log_note "Ensure invalid characters fail"
for POOLNAME in 0 1 2 3 4 5 6 7 8 9 2222222222222222222
do
	log_mustnot $ZPOOL create -m $TESTDIR $POOLNAME $DISK
	log_mustnot $ZPOOL destroy $POOLNAME
done

log_note "Check that invalid octal values fail"
for oct in "\000" "\001" "\002" "\003" "\004" "\005" "\006" "\007" \
    "\010" "\011" "\012" "\013" "\014" "\015" "\017" \
    "\020" "\021" "\022" "\023" "\024" "\025" "\026" "\027" \
    "\030" "\031" "\032" "\033" "\034" "\035" "\036" "\037" \
    "\040" "\177"
do
	# Be careful not to print the poolname, because it might be a terminal
	# control character
	POOLNAME=`eval "print x | tr 'x' '$oct'"`
	$ZPOOL create -m $TESTDIR $POOLNAME $DISK > /dev/null 2>&1
	if [ $? = 0 ]; then
		log_fail "Unexpectedly created pool: \"$oct\""
	elif poolexists $POOLNAME; then
		log_fail "Unexpectedly created pool: \"$oct\""
	fi

	$ZPOOL destroy $POOLNAME > /dev/null 2>&1
	if [ $? = 0 ]; then
		log_fail "Unexpectedly destroyed pool: \"$oct\""
	fi
done

log_note "Verify invalid pool names fail"	
set -A POOLNAME "c0t0d0s0" "c0t0d0" "c0t0d19" "c0t50000E0108D279d0" \
    "mirror" "raidz" ",," ",,,,,,,,,,,,,,,,,,,,,,,,," \
    "mirror_pool" "raidz_pool" \
    "mirror-pool" "raidz-pool" "spare" "spare_pool" \
    "spare-pool" "raidz1-" "raidz2:" ":aaa" "-bbb" "_ccc" ".ddd"
POOLNAME[${#POOLNAME[@]}]='log'
typeset -i i=0
while ((i < ${#POOLNAME[@]})); do
	log_mustnot $ZPOOL create -m $TESTDIR ${POOLNAME[$i]} $DISK
        if poolexists ${POOLNAME[$i]}; then
                log_fail "Unexpectedly created pool: '${POOLNAME[$i]}'"
        fi

	log_mustnot $ZPOOL destroy ${POOLNAME[$i]}        

	((i += 1))
done

log_pass "Invalid names and characters were caught correctly"
