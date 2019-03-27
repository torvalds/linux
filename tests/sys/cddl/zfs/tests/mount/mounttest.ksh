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
#
# ident	"@(#)mounttest.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: mounttest
#
# DESCRIPTION:
# zfs mount and unmount commands should mount and unmount existing
# file systems.
#
# STRATEGY:
# 1. Call zfs mount command
# 2. Make sure the file systems were mounted
# 3. Call zfs unmount command
# 4. Make sure the file systems were unmounted
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


unmountcmd=""
while getopts u: OPT ; do
	case $OPT in
		u)  unmountcmd=$OPTARG
		;;
		?)  log_fail Usage: $0 [-u unmountcmd]
		;;
	esac
done

log_note Mount file systems
typeset -i i=1 
for fs in $TESTFSS ; do
	log_must $ZFS mount $fs
	((i = i + 1))
done

log_note Make sure the file systems were mounted
for fs in $TESTFSS ; do
	mounted $fs || log_fail File system $fs not mounted
done

log_note Unmount the file systems
if [[ $unmountcmd = *all ]] ; then
	log_must $ZFS $unmountcmd -F zfs
else
	if [[ $unmountcmd = *-a ]] ; then
		log_must $ZFS $unmountcmd
	else
		for fs in $TESTFSS ; do
			log_must $ZFS $unmountcmd $fs
		done
	fi
fi  

log_note Make sure the file systems were unmounted
for fs in $TESTFSS ; do
	unmounted $fs || log_fail File system $fs not unmounted
done

log_pass All file systems are unmounted 
