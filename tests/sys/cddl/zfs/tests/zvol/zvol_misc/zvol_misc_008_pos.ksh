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
# ident	"@(#)zvol_misc_008_pos.ksh	1.3	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_misc_008_pos
#
# DESCRIPTION:
# Verify that device nodes are modified appropriately during zfs command
# operations on volumes.
#
# STRATEGY:
# For a certain number of iterations, with root setup for each test set:
#   - Recursively snapshot the root.
#   - Clone the volume to another name in the root.
#   - Promote the clone.
#   - Demote the original clone.
#   - Snapshot & clone the clone.
#   - Rename the root.
#   - Destroy the renamed root.
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

log_assert "Verify that ZFS volume device nodes are handled properly (part 2)."

ROOTPREFIX=$TESTPOOL/008
DIRS="dir0 dir1"
VOLS="vol0 dir0/dvol0 dir1/dvol1"

typeset -i NUM_ITERATIONS=10

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

function test_exists
{
	for zvolds in $*; do
		log_must test -c /dev/zvol/${zvolds}
	done
}

function test_notexists
{
	for zvolds in $*; do
		log_mustnot test -e /dev/zvol/${zvolds}
	done
}

typeset -i i=0
while (( i != NUM_ITERATIONS )); do
	root=${ROOTPREFIX}_iter${i}
	# Test set 2: Recursive snapshot, cloning/promoting, and root-rename
	root_setup $root
	log_must $ZFS snapshot -r $root@snap
	log_must $ZFS clone $root/vol0@snap $root/vol1
	test_exists $root/vol1
	test_notexists $root/vol1@snap

	log_must $ZFS promote $root/vol1
	test_exists $root/vol0 $root/vol1 $root/vol1@snap
	test_notexists $root/vol0@snap

	# Re-promote the original volume.
	log_must $ZFS promote $root/vol0
	test_exists $root/vol0 $root/vol1 $root/vol0@snap
	test_notexists $root/vol1@snap

	# Clone a clone's snapshot.
	log_must $ZFS snapshot $root/vol1@newsnap
	log_must $ZFS clone $root/vol1@newsnap $root/vol2
	test_exists $root/vol2
	test_notexists $root/vol2@snap

	# Now promote *that* clone.
	log_must $ZFS promote $root/vol2
	test_exists $root/vol0 $root/vol0@snap \
		$root/vol1 $root/vol2 $root/vol2@newsnap
	test_notexists $root/vol1@snap $root/vol1@newsnap

	renamed=${root}_renamed
	log_must $ZFS rename $root $renamed
	# Ensure that the root rename applies to clones and promoted clones.
	test_exists $renamed/vol1 $renamed/vol2 $renamed/vol2@newsnap
	test_notexists $root/vol1 $renamed/vol1@snap $renamed/vol1@newsnap
	for vol in $VOLS; do
		test_notexists $root/$vol $root/$vol@snap
		test_exists $renamed/$vol $renamed/$vol@snap
	done

	log_must $ZFS destroy -r $renamed
	test_notexists $renamed/vol0 $renamed/vol1 $renamed/vol2

	(( i += 1 ))
done
log_pass "ZFS volume device nodes are handled properly (part 2)."
