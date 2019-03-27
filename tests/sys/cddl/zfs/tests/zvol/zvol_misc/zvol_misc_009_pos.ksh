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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zvol_misc_009_pos.ksh	1.3	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_misc_009_pos
#
# DESCRIPTION:
# Verify that device nodes are modified appropriately during zfs command
# operations on volumes.
#
# STRATEGY:
# For a certain number of iterations, with root setup for each test set:
#   - Recursively snapshot the root.
#   - Send|Receive the root to another root.
#   - Destroy the original and received roots.
#
# At each stage, the device nodes are checked to match the expectations.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-03-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Verify that ZFS volume device nodes are handled properly (part 3)."

ROOTPREFIX=$TESTPOOL/009
DIRS="dir0 dir1"
VOLS="vol0 dir0/dvol0 dir1/dvol1"

typeset -i NUM_ITERATIONS=5

function onexit_callback
{
	log_must $ZFS list -t all
	log_note "Char devices in /dev/zvol:"
	find /dev/zvol -type c
}
log_onexit onexit_callback

function root_setup
{
	rootds=$1

	log_must $ZFS create $rootds
	for dir in $DIRS; do
		log_must $ZFS create $rootds/$dir
	done
	for vol in $VOLS; do
		log_must $ZFS create -V 100M $rootds/$vol
		log_must test -c /dev/zvol/$rootds/$vol
	done
}

typeset -i i=0
while (( i != NUM_ITERATIONS )); do
	root=${ROOTPREFIX}_iter${i}
	# Test set 3: send|receive & receive with forced rollback
	root_setup $root
	log_must $ZFS snapshot -r $root@snap
	received=${root}_recv
	log_must eval "$ZFS send -R $root@snap | $ZFS receive -F ${received}"
	for vol in $VOLS; do
		log_must test -c /dev/zvol/${received}/${vol}
		log_must test -c /dev/zvol/${received}/${vol}@snap
	done
	# Re-send with -F, to ensure that actual receive rollback also works.
	# For good measure, pre-destroy one of the volumes.  Also, destroy
	# the snapshot we received earlier for this to work.
	log_must $ZFS destroy -r $received/vol0
	log_must $ZFS destroy -r $received@snap
	log_must eval "$ZFS send -R $root@snap | $ZFS receive -F ${received}"
	for vol in $VOLS; do
		log_must test -c /dev/zvol/${received}/${vol}
		log_must test -c /dev/zvol/${received}/${vol}@snap
	done
	log_must $ZFS destroy -r ${root}
	log_mustnot test -e /dev/zvol/${root}/vol0
	log_mustnot test -e /dev/zvol/${root}/vol0@snap
	log_must $ZFS destroy -r ${received}
	log_mustnot test -e /dev/zvol/${received}/vol0
	log_mustnot test -e /dev/zvol/${received}/vol0@snap

	(( i += 1 ))
done
log_pass "ZFS volume device nodes are handled properly (part 3)."
